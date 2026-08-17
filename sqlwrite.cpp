/*
 * sqlWrite.cpp
 * Copyright 2025-2026 AutoZone, Inc.
 *
 * Provides SQL equivalents of EZ-C write actions
 */
/* System Headers */
#include <unistd.h>      // fork(), _exit(), popen(), unlink(), gethostname()
#include <sys/types.h>   // pid_t
#include <sys/stat.h>    // chmod()
#include <sys/wait.h>    // For future zombie process handling
#include <errno.h>       // errno variable
#include <string.h>      // strerror()
#include <stdio.h>       // popen(), fgets()
#include <time.h>        // time()
#include <libpq-fe.h>    // PostgreSQL libpq C API (for COPY streaming)
#include <QString>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDriver>
#include <QJsonObject>
#include <QJsonDocument>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

/* Local Headers */
#include "datafile.h"
#include "ezcMD.h"
#include "sqlCommon.h"

using namespace SqlCommon;

/* Macros */
#define PROTECT_FIELD_IDX   2

/* Convenience macros for working with bitflags */
#define DOPEN_IS_BATCH(flags)       ((flags) & DF_BATCH)

/* COPY FROM STDIN Configuration Constants */
#define COPY_TEMP_PREFIX       ".ezc_copy_" /* Prefix for temp filenames */
#define COPY_NULL_MARKER       "\\N"        /* NULL marker in colon-separated format */
#define COPY_FIELD_SEPARATOR   ":"          /* Field separator in colon-separated format */

/* Structures and Unions */
/*  Encapsulates needed Postgres table info from a DFILE record */
struct TableData {
    QStringList columns;         /* Column names in table schema order */
    QStringList values;          /* Value placeholders (":colname" or "NULL") */
    QMap<QString, QString> bindings; /* Map of actual values for binding */
};

/* External Variables */
/* Global Variables */
/* Local Variables */

/* External Prototypes */
extern "C" EZCMD* ezcmd_lookup(const char* header_name, int format_num);

/* Local Prototypes */
static char* my_etroot(void);
static void sqlWrite(const DFILE* dfd, int operation);
static void sqlCreate(const char* dfname);
static void sqlCommit(DFILE* dfd);
static void sqlRollback(DFILE* dfd);
static QString getHeaderFileName(const char * dfname);
static QStringList getTableColumns(const QString& tableName);

/* Table Data Extraction Helper Functions */
static TableData getTableDataFromDfile(const DFILE* dfd, int operation = DF_UPDATE);

/* Batched INSERT Helper Functions */
static long getCurrentTimeMs(void);
static QString escapeSqlValue(const QString& value);
static void appendToBatchBuffer(DFILE* dfd, const QString& valuesTuple);
static bool shouldFlushBatch(DFILE* dfd);
static void sqlFlushInsertBatch(DFILE* dfd);
static void accumulateInsertValues(DFILE* dfd);

/* Transaction Management Helper Functions */
static void sqlBegin(DFILE* dfd);

/* Caller-based batch mode detection */
static bool useBatchMode(DFILE* dfd);

/* Postgres COPY Helper Functions */
static char* generateTempFilename(void);
static bool usePostgresCopy(DFILE* dfd);
static void sqlWriteBatch(DFILE* dfd);
static void sqlCopy(DFILE* dfd);
static void ezcRecordToCopyFormat(DFILE* dfd, QString& output);
static QString buildCopyCommand(DFILE* dfd);
#ifdef USE_PQ_API
static bool sqlCopyViaLibpq(DFILE* dfd, const QString& copyCmd);
#else
static bool sqlCopyViaShell(DFILE* dfd, const QString& copyCmd);
#endif

/*	Name: sql_open_hook()
 *
 *	Description:
 *		Hook function called by dopen() to set up SQL insertion policy for the
 *		associated dfd.
 *
 *	ALGORITHM:
 *		Validate DFILE pointer
 *      Initialize fields related to batch insert based on DFILE mode
 *      Initialize copy-from-stdin fields (initially dormant)
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		dfd (DFILE*)	Open data file with active batch transaction (IN)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : January 21, 2026
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 */

extern "C" void sql_open_hook(DFILE* dfd)
{
    if (!dfd) {
        qWarning() << "Invalid DFILE passed to sql_open_hook()";
        return;
    }

	/* Determine if SQL operations should be active for this datafile */
	dfd->is_sql_active = FALSE;  // Default to inactive

	QString headerFileName = getHeaderFileName(dfd->filename);
	if (!headerFileName.isEmpty()) {
		QString ddlFileName = QString("%1/files/ezcddls/%2.sql")
							  .arg(my_etroot())
							  .arg(headerFileName);
		QFileInfo ddlFile(ddlFileName);
		if (ddlFile.exists() && ddlFile.isFile()) {
			dfd->is_sql_active = TRUE;
		}
	}

    qDebug() << "SQL"
        << ((dfd->is_sql_active == TRUE) ? "active" : "inactive")
        << "for datafile:" << dfd->filename;

	/* See if we opened in SQL batch mode */
	int batch_mode = DOPEN_IS_BATCH(dfd->mode) ? TRUE : FALSE;
	dfd->batch_mode = batch_mode;
	dfd->batch_transaction_started = FALSE;

	/* Allocate batch buffer only if batch mode enabled */
	if (batch_mode == TRUE) {
		dfd->batch_buffer = (char *)malloc(getBatchBufferInitialSize());
		if (dfd->batch_buffer == NULL) {
			qWarning() << "Failed to allocate batch buffer in dopen(), size: "
                << getBatchBufferInitialSize();
			/* Allocation failure is not fatal - continue without batching */
			dfd->batch_mode = FALSE;
		} else {
			dfd->batch_buffer_size = 0;
			dfd->batch_buffer_capacity = getBatchBufferInitialSize();
			dfd->batch_insert_count = 0;
			dfd->batch_last_flush_time = 0;
		}
	} else {
		/* Non-batch mode: initialize to NULL/0 for safety */
		dfd->batch_buffer = NULL;
		dfd->batch_buffer_size = 0;
		dfd->batch_buffer_capacity = 0;
		dfd->batch_insert_count = 0;
		dfd->batch_last_flush_time = 0;
	}

	/* Initialize fields related to COPY FROM STDIN batching */
    /* (this is probably overkill) */
	dfd->copy_mode = FALSE;
	dfd->copy_record_count = COPY_MODE_UNCHECKED;
	dfd->copy_temp_filename = NULL;
	dfd->copy_temp_file = NULL;
}

/*	Name: sqlWriteBatch()
 *
 *	Description:
 *		Handles batched INSERT logic.
 *
 *	Arguments:
 *		dfd (DFILE*) - Open data file with current record (IN/OUT)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : December 10, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  12/10/2025	jgilreath	Extracted from sql_write_hook()
 */
static void sqlWriteBatch(DFILE* dfd)
{
    if (!dfd) {
        qWarning() << "Invalid DFILE passed to sqlWriteBatch()";
        return;
    }

    /* Accumulate record instead of writing immediately */
    accumulateInsertValues(dfd);
    dfd->batch_insert_count++;

    /* Check if batch should be flushed */
    if (shouldFlushBatch(dfd)) {
        sqlFlushInsertBatch(dfd);
    }
}	/* sqlWriteBatch() */

/*	Name: sqlCopy()
 *
 *	Description:
 *		Handles accumulation for colon-separated temp file.
 *		Called for each record when copy_mode is TRUE.
 *		Formats record and writes to colon-separated staging file.
 *
 *	Algorithm:
 *		1. Validate DFILE and temp file
 *		2. Call ezcRecordToCopyFormat() to format record as colon-separated line
 *		3. Write formatted line to dfd->copy_temp_file
 *		4. Increment dfd->copy_record_count
 *		5. Handle file I/O errors gracefully
 *
 *	Arguments:
 *		dfd (DFILE*) - Open data file with current record (IN/OUT)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : December 10, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  12/10/2025	jgilreath	Initial implementation
 */
static void sqlCopy(DFILE* dfd)
{
    if (!dfd || !dfd->crec) {
            qWarning() << "[PGCopy] Invalid DFILE in sqlCopy()";
            return;
        }

        if (!dfd->copy_temp_file) {
            qWarning() << "[PGCopy] Temp file not open in sqlCopy()";
        return;
    }

    /* Format record as colon-separated line */
    QString copiLine;
    ezcRecordToCopyFormat(dfd, copiLine);

    if (copiLine.isEmpty()) {
        qWarning() << "[PGCopy] Failed to format record for COPY";
        return;
    }

    /* Write to temp file */
    QByteArray lineBytes = copiLine.toUtf8();
    size_t written = fwrite(lineBytes.constData(), 1, lineBytes.length(), dfd->copy_temp_file);

    if (written != (size_t)lineBytes.length()) {
        qWarning() << "[PGCopy] Short write to temp file: wrote" << written << "of" << lineBytes.length();
        return;
    }

    /* Write newline */
    if (fwrite("\n", 1, 1, dfd->copy_temp_file) != 1) {
        qWarning() << "[PGCopy] Failed to write newline to temp file";
        return;
    }

    /* Increment record count */
    dfd->copy_record_count++;

    if ((dfd->copy_record_count % 10000) == 0) {
        qDebug() << "[PGCopy] Accumulated" << dfd->copy_record_count << "records to temp file";
    }
}	/* sqlCopy() */

/*	Name: ezcRecordToCopyFormat()
 *
 *	Description:
 *		Formats an EZ-C record as a colon-separated line for PostgreSQL COPY FROM STDIN.
 *		Uses getTableDataFromDfile() to extract field values with same mapping,
 *		NULL handling, and JSONB accumulation. Applies COPY-specific formatting:
 *		colon delimiters, COPY null markers (\N), and escaping of special chars.
 *
 *	Algorithm:
 *		1. Extract all field mapping and values via getTableDataFromDfile()
 *		2. Build colon-separated output line in table schema order:
 *		   - id (record seek position)
 *		   - format_type (format number)
 *		   - All other columns (NULL or value)
 *		   - attributes (JSONB if unmapped fields exist)
 *		3. Trim whitespace from extracted field values
 *		4. Escape special characters for COPY TEXT format
 *		5. Use \N for NULL values (PostgreSQL COPY standard)
 *
 *	Arguments:
 *		dfd (DFILE*) - Open data file with current record (IN)
 *		output (QString&) - Output line in colon-separated COPY format (OUT)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : December 10, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  12/10/2025	jgilreath	Initial implementation
 *  12/16/2025	jgilreath	Refactored to use getTableDataFromDfile() for field extraction
 */
static void ezcRecordToCopyFormat(DFILE* dfd, QString& output)
{
    output.clear();

    if (!dfd || !dfd->crec) {
        qWarning() << "[PGCopy] Invalid DFILE in ezcRecordToCopyFormat()";
        return;
    }

    /* Use EZ-C data to get necessary table information */
    TableData td = getTableDataFromDfile(dfd, DF_UPDATE);
    if (td.columns.isEmpty()) {
        qWarning() << "[PGCopy] Failed to extract table data for ezcRecordToCopyFormat()";
        return;
    }

    int fmtNum = dfd->crec->recfmt;

    /* Build output in table schema order (id, format_type, then other columns) */

    /* Always start with id - use record seek position as ID */
    output += QString::number(dfd->seekp);

    /* Add format_type */
    output += COPY_FIELD_SEPARATOR;
    output += QString::number(fmtNum);

    /* Add directly mapped columns in table schema order */
    for (const QString& col : td.columns) {
        /* Skip system columns (handled above) and attributes and timestamps (handled below) */
        if (col == "id"
            || col == "format_type"
            || col == "attributes"
            || col == "created_at"
            || col == "updated_at") {
            continue;
        }

        output += COPY_FIELD_SEPARATOR;

        if (td.bindings.contains(col)) {
            /* Column has a value from EZCMD */
            QString value = td.bindings[col];
            /* Trim whitespace (EZ-C pads fields with spaces) */
            value = value.trimmed();
            if (value.isEmpty()) {
                output += COPY_NULL_MARKER;
            } else {
                /* Escape special characters for COPY TEXT format */
                value.replace("\\", "\\\\");  /* Backslash first */
                value.replace(":", "\\:");    /* Escape colon */
                value.replace("\n", "\\n");   /* Escape newline */
                value.replace("\r", "\\r");   /* Escape carriage return */
                output += value;
            }
        } else {
            /* Column not in this format - use NULL marker */
            output += COPY_NULL_MARKER;
        }
    }

    /* Add JSONB attributes column if unmapped fields exist */
    output += COPY_FIELD_SEPARATOR;
    if (td.bindings.contains("attributes")) {
        QString jsonValue = td.bindings["attributes"];
        /* Escape special characters in JSON string */
        jsonValue.replace("\\", "\\\\");
        jsonValue.replace(":", "\\:");
        jsonValue.replace("\n", "\\n");
        jsonValue.replace("\r", "\\r");
        output += jsonValue;
    }

    /* Add timestamps; I see no benefit to calling this potentially 10,000s of times */
    static QString currentTimestamp;
    if (currentTimestamp.isEmpty()) {
        currentTimestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        currentTimestamp.replace(":", "\\:");  // to prevent being seen as a delimter
    }
    output += COPY_FIELD_SEPARATOR;
    output += currentTimestamp;         // created
    output += COPY_FIELD_SEPARATOR;
    output += currentTimestamp;         // last updated

}	/* ezcRecordToCopyFormat() */

/*	Name: my_etroot()
 *
 *	Description:
 *		Returns a pointer to a null-terminated string containing the value of the
 *		ETROOT environment variable, or a default value if not set.
 *
 *	ALGORITHM:
 *		If ETROOT not yet known:
 *			If value of ETROOT is valid:
 *				Save it
 *			Else:
 *				Use default value (/user/sms)
 *		Return pointer to static variable
 *
 *	Arguments:
 *		NONE
 *
 *	Return Values:
 *		char* - Pointer to string containing ETROOT value or default
 *
 *	Author: AutoZone (copied from sms/library/az/etroot.c)
 *	Date  : December 12, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  12/12/2025	jgilreath	Copied from etroot.c for use in sql_write.cpp
 */
static char* my_etroot(void)
{
	char		*tmp_p;			/* temporary pointer to value of ETROOT */
	static char etroot[128];	/* local copy of ETROOT env. variable */

	/* if ETROOT not yet known */
	if (*etroot == 0)
		{
		/* if value of ETROOT valid */
		if ((tmp_p=getenv("ETROOT")) != NULL)
			{
			/* copy into static location */
			strncpy(etroot, tmp_p, sizeof(etroot));

			/* if etroot is huge, there is probably corruption */
			if (strlen(etroot) == (sizeof(etroot) - 1))
				{
				/* so use default value */
				strcpy(etroot, "/user/sms");
				}
			}
		/* else */
		else
			{
			/* use default value */
			strcpy(etroot, "/user/sms");
			}
		}

	/* return pointer to etroot */
	return(etroot);
}	/* my_etroot() */

/*	Name: getHeaderFileName()
 *
 *	Description:
 *		Extracts the C header filename associated with a DFILE
 *		Handles exception mappings (e.g., "dmdhist0" → "dmdhist")
 *
 *	ALGORITHM:
 *		1. Remove ".df" extension from DFILE filename
 *		2. Check exceptions hash for unconventional file name mappings
 *		3. Return mapped name or original name if no exception
 *
 *	Arguments:
 *		dfname (const char*) - DFILE filename (e.g., "kepaitem.df") (IN)
 *
 *	Return Values:
 *		QString - Basename of associated header file (e.g., "kepaitem"), empty if error
 *
 *	Author: John Gilreath
 *	Date  : December 12, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  12/12/2025	jgilreath	Redefined: returns table name without schema prefix
 *  12/12/2025	jgilreath	Removed redundant QFileInfo::baseName() call
 */
static QString getHeaderFileName(const char * dfname)
{
	static QHash<QString, QString> exceptions = {
			{ "dmdhist0", "dmdhist" },
			{ "dmdhist1", "dmdhist" },
			{ "dmdhist2", "dmdhist" },
			{ "dmdhist3", "dmdhist" },
			{ "dmdhist4", "dmdhist" },
			{ "dmdhist5", "dmdhist" },
			{ "dmdhist6", "dmdhist" },
			{ "kcomcuscont", "kcomcuscontact" },
			{ "kcomcushday", "kcomcusholiday" },
			{ "kcomcushour", "kcomcushours" },
			{ "kcomcusplan", "kcomcuspricpln" },
			{ "kcomcusstor", "kcomcusstore" },
			{ "invsusp1", "invsusp" },
			{ "invsusp2", "invsusp" },
			{ "invsusp3", "invsusp" },
			{ "invsuspcrtr", "invsuspcrtra" },
			{ "itemadjhist", "kitemadj" },
			{ "vdpordhist", "vdporder" },
	};

	if (!dfname) {
		return QString();
	}

	/* Remove ".df" extension if present */
	QString headerFileName = QString(dfname).remove(".df");

	/* Check for non-standard file name mappings */
	QHash<QString, QString>::const_iterator it =
        exceptions.find(headerFileName);
	if (it != exceptions.end()) {
		return it.value();
	}

	return headerFileName;
}	/* getHeaderFileName() */

/*	Name: sql_write_hook()
 *
 *	Description:
 *		Hook called from dwinsert() and dwriterec() after successful record operations.
 *		Syncs the EZ-C record to PostgreSQL.
 *
 *	ALGORITHM:
 *		Validate DFILE pointer and current record
 *		Validate operation type (DF_UPDATE only)
 *		Call sqlWrite() to perform the actual conversion and sync
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		dfd (DFILE*)	Open data file with current record (IN)
 *		operation (int)	DF_UPDATE (IN)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : November  7, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  11/14/2025	jgilreath	Renamed to sql_write_hook(), added operation parameter
 */
extern "C" void sql_write_hook(DFILE* dfd, int operation)
{
    if (!dfd || !dfd->crec) {
        qWarning() << "Invalid DFILE passed to sql_write_hook()";
        return;
    }

    if (!dfd->is_sql_active) {
        return;  // SQL not active for this datafile
    }

    if (operation != DF_UPDATE) {
        qWarning() << "Invalid operation passed to sql_write_hook():" << operation;
        return;
    }

    /* Route to appropriate handler based on batch mode and copy_mode */
    if (useBatchMode(dfd)) {
        /* Start transaction on first write if not already started */
        if (!dfd->batch_transaction_started) {
            sqlBegin(dfd);
        }

        /* Allocate batch buffer on-demand if not already allocated */
        if (dfd->batch_buffer == NULL) {
            dfd->batch_buffer = (char *)malloc(getBatchBufferInitialSize());
            if (dfd->batch_buffer == NULL) {
                qWarning() << "Failed to allocate batch buffer in sql_write_hook()";
                return;
            }
            dfd->batch_buffer_size = 0;
            dfd->batch_buffer_capacity = getBatchBufferInitialSize();
            dfd->batch_insert_count = 0;
            dfd->batch_last_flush_time = 0;
        }

        /* Evaluate whether PostgreSQL COPY should be used for this data file */
        usePostgresCopy(dfd);

        if (dfd->copy_mode) {
            /* COPY FROM STDIN: accumulate to colon-separated temp file */
            sqlCopy(dfd);
        } else {
            /* Batched INSERT statements */
            sqlWriteBatch(dfd);
        }
    } else {
        sqlWrite(dfd, operation);
    }
}	/* sql_write_hook() */

/*	Name: sql_delete_hook()
 *
 *	Description:
 *		Hook called from ddelete() after successful record deletion.
 *		Syncs the EZ-C record deletion to PostgreSQL.
 *		Ensures the corresponding row is deleted from the PostgreSQL table.
 *
 *	ALGORITHM:
 *		Validate DFILE pointer
 *		Call sqlWrite() to perform the DELETE operation
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		dfd (DFILE*)	Open data file with record to delete (IN)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : November 19, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  11/19/2025	jgilreath	Initial implementation for PostgreSQL DELETE synchronization
 */
extern "C" void sql_delete_hook(DFILE* dfd)
{
    if (!dfd) {
        qWarning() << "Invalid DFILE passed to sql_delete_hook()";
        return;
    }

    if (!dfd->is_sql_active) {
        return;  // SQL not active for this datafile
    }

    sqlWrite(dfd, DF_DELETE);
}	/* sql_delete_hook() */

/*	Name: buildCopyCommand()
 *
 *	Description:
 *		Builds a PostgreSQL COPY FROM STDIN command
 *		Uses COPY FROM STDIN instead of FROM '<file>' to avoid superuser requirements.
 *		Extracts column names from EZCMD metadata.
 *
 *	Arguments:
 *		dfd (DFILE*) - Open data file with current record (IN)
 *
 *	Return Values:
 *		QString - Complete COPY FROM STDIN command ready for execution
 *
 *	Author: John Gilreath
 *	Date  : December 10, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  12/10/2025	jgilreath	Initial implementation
 *  12/10/2025	jgilreath	Changed to COPY FROM STDIN (avoids superuser requirement)
 */
static QString buildCopyCommand(DFILE* dfd)
{
    if (!dfd) {
        qWarning() << "[PGCopy] Invalid DFILE in buildCopyCommand()";
        return QString();
    }

    /* Get associated header file needed for metadata */
    QString headerFileName = getHeaderFileName(dfd->filename);
    if (headerFileName.isEmpty()) {
        qWarning() << "[PGCopy] Could not get table name in buildCopyCommand()";
        return QString();
    }

    int fmtNum = dfd->crec->recfmt;

    /* Look up EZCMD metadata */
    EZCMD* md = ezcmd_lookup(headerFileName.toUtf8().constData(), fmtNum);
    if (!md) {
        qWarning() << "[PGCopy] Metadata not found for COPY command for header file =" << headerFileName;
        return QString();
    }

    /* Get table columns (pass unqualified name) */
    QString tableName = QString(dfd->filename).remove(".df");
    QStringList allTableColumns = getTableColumns(tableName);
    if (allTableColumns.isEmpty()) {
        qWarning() << "[PGCopy] Could not retrieve columns for table:" << tableName;
        return QString();
    }

    /* Build column list from table schema */
    QStringList columnList;
    columnList << "id" << "format_type";
    for (const QString& col : allTableColumns) {
        if (col != "id" && col != "format_type") {
            columnList << col;
        }
    }

    /* Build COPY FROM STDIN command (with schema prefix for SQL) */
    QString pgTableName = "ezc." + tableName;
    QString copyCmd = QString(
        "COPY %1 (%2) FROM STDIN "
        "WITH (FORMAT text, DELIMITER ':', NULL '\\N', ENCODING 'utf-8')")
        .arg(pgTableName)
        .arg(columnList.join(", "));

    qDebug() << "[PGCopy] Built COPY command:" << copyCmd;

    return copyCmd;
}

/*	Name: usePostgresCopy()
 *
 *	Description:
 *		Determines whether to use PostgreSQL COPY FROM STDIN or batched INSERT
 *		for the given data file. Encapsulates all decision logic in one place for easy
 *		refinement of criteria.
 *
 *	ALGORITHM:
 *		1. Extract table name from DFILE filename
 *		2. Check if table matches configured table name
 *		3. If so, allocate and open temp file
 *		4. Set dfd->copy_mode flag based on success
 *		5. Return copy_mode state
 *
 *	Arguments:
 *		dfd (DFILE*) - Open data file to evaluate (IN/OUT)
 *
 *	Return Values:
 *		bool - TRUE if COPY should be used, FALSE if regular inserts should be used
 *
 *	Side Effects:
 *		- Sets dfd->copy_mode (TRUE or FALSE)
 *		- Allocates dfd->copy_temp_filename if COPY activated
 *		- Opens dfd->copy_temp_file if COPY activated
 *
 *	Author: John Gilreath
 *	Date  : December 12, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  12/12/2025	jgilreath	Extracted from sqlBegin() for clarity and future refinement
 */
static bool usePostgresCopy(DFILE* dfd)
{
    if (!dfd) {
        qWarning() << "[PGCopy] Invalid DFILE passed to usePostgresCopy()";
        return false;
    }

    /* Skip if already checked for this DFILE */
    if (dfd->copy_record_count != COPY_MODE_UNCHECKED) {
        return dfd->copy_mode;
    }

    /* Initialize to FALSE - assume regular inserts by default */
    dfd->copy_mode = FALSE;
    dfd->copy_temp_filename = NULL;

    /* Extract table name from filename (remove .df extension) */
    QString basename = QString::fromUtf8(dfd->filename).remove(".df");

    /* List of tables that always use COPY FROM STDIN */
    QStringList copyTables;
    copyTables << "kepaitem" << "kupcnum" << "knatprice" << "klbldsc";

    qDebug() << "[PGCopy] Evaluating table:" << basename << "for COPY eligibility";

    /* Check if table is in the COPY tables list */
    if (!copyTables.contains(basename)) {
        qDebug() << "[PGCopy] Table" << basename << "is not in COPY tables list";
        dfd->copy_record_count = COPY_MODE_DISABLED;
        return false;
    }

    /* Criterion: Must successfully allocate and open temp file */
    qDebug() << "[PGCopy] Table" << basename << "is in COPY list - activating COPY";

    char* tempFilename = generateTempFilename();
    dfd->copy_temp_filename = (char*)malloc(strlen(tempFilename) + 1);
    if (!dfd->copy_temp_filename) {
        qWarning() << "[PGCopy] Failed to allocate memory for temp filename";
        dfd->copy_record_count = COPY_MODE_DISABLED;
        return false;
    }

    strcpy(dfd->copy_temp_filename, tempFilename);

    /* Open temp file for writing */
    dfd->copy_temp_file = fopen(dfd->copy_temp_filename, "wb");
    if (!dfd->copy_temp_file) {
        qWarning() << "[PGCopy] Failed to open temp file:" << dfd->copy_temp_filename;
        free(dfd->copy_temp_filename);
        dfd->copy_temp_filename = NULL;
        dfd->copy_record_count = COPY_MODE_DISABLED;
        return false;
    }

    /* Set restrictive permissions on temp file */
    chmod(dfd->copy_temp_filename, 0600);
    qDebug() << "[PGCopy] Opened temp file with restricted permissions:" << dfd->copy_temp_filename;

    /* Activate COPY mode */
    dfd->copy_mode = TRUE;
    dfd->copy_record_count = 0;

    return true;
}

#ifdef USE_PQ_API
/*	Name: sqlCopyViaLibpq()
 *
 *	Description:
 *		Executes PostgreSQL COPY command using libpq C API for streaming data.
 *		Uses the underlying libpq connection from Qt's SQL driver to maintain
 *		transaction isolation and table locking during the load.
 *
 *		This function is isolated for easy replacement with C-based implementation
 *		or alternative transport mechanisms (e.g., COPY FROM file, batch inserts).
 *
 *	ALGORITHM:
 *		1. Get underlying libpq connection from Qt SQL driver
 *		2. Open temp file for reading
 *		3. Send COPY command to PostgreSQL
 *		4. Stream data line-by-line from temp file using PQputCopyData()
 *		5. End COPY with PQputCopyEnd()
 *		6. Read and validate COPY result
 *		7. Handle errors with proper cleanup
 *
 *	Arguments:
 *		dfd (DFILE*) - Open data file with temp filename and record count (IN)
 *		copyCmd (QString&) - Complete COPY command to execute (IN)
 *
 *	Return Values:
 *		bool - TRUE if COPY succeeded, FALSE if COPY failed or error occurred
 *
 *	Author: John Gilreath
 *	Date  : December 12, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  12/12/2025	jgilreath	Initial implementation using libpq C API
 *
 *	NOTE: This function is designed to be easily replaced. If migrating to
 *	pure C implementation, simply replace this function with C-based version.
 */
static bool sqlCopyViaLibpq(DFILE* dfd, const QString& copyCmd)
{
    if (!dfd || copyCmd.isEmpty()) {
        qWarning() << "[PGCopy] Invalid parameters to sqlCopyViaLibpq()";
        return false;
    }

    QSqlDatabase db = getDatabase();
    if (!db.isOpen()) {
        qWarning() << "[PGCopy] Database connection not available for libpq COPY";
        return false;
    }

    qDebug() << "[PGCopy] Database is open, attempting to get driver handle";

    /* Get underlying libpq connection from Qt driver */
    if (!db.driver()) {
        qWarning() << "[PGCopy] Could not get QSqlDriver from database";
        return false;
    }

    QVariant handleVar = db.driver()->handle();
    qDebug() << "[PGCopy] Got handle variant, type:" << handleVar.typeName();

    if (!handleVar.isValid()) {
        qWarning() << "[PGCopy] Handle variant is not valid";
        return false;
    }

    /* For QPSQL driver, handle() returns a QVariant containing a pointer to PGconn* */
    PGconn** ptrPtr = static_cast<PGconn**>(handleVar.data());
    if (!ptrPtr || !(*ptrPtr)) {
        qWarning() << "[PGCopy] Could not extract PGconn* from handle variant";
        return false;
    }

    PGconn* conn = *ptrPtr;
    qDebug() << "[PGCopy] Successfully extracted PGconn* from Qt driver";

    /* Send COPY command to PostgreSQL */
    if (PQsendQuery(conn, copyCmd.toUtf8().constData()) == 0) {
        qWarning() << "[PGCopy] PQsendQuery failed:" << PQerrorMessage(conn);
        return false;
    }

    qDebug() << "[PGCopy] Sent COPY command to PostgreSQL via libpq";

    /* Get initial response to put connection into COPY_IN mode */
    /* This is required before calling PQputCopyData() */
    PGresult* initRes = PQgetResult(conn);
    if (!initRes) {
        qWarning() << "[PGCopy] PQgetResult failed after COPY command";
        return false;
    }

    ExecStatusType initStatus = PQresultStatus(initRes);
    if (initStatus != PGRES_COPY_IN) {
        qWarning() << "[PGCopy] COPY command did not enter COPY_IN mode, status:" << initStatus
                   << "error:" << PQresultErrorMessage(initRes);
        PQclear(initRes);
        return false;
    }

    PQclear(initRes);
    qDebug() << "[PGCopy] Connection in COPY_IN mode, ready to stream data";

    /* Open temp file for reading */
    FILE* tempFile = fopen(dfd->copy_temp_filename, "r");
    if (!tempFile) {
        qWarning() << "[PGCopy] Failed to open temp file for reading:" << dfd->copy_temp_filename;
        PQputCopyEnd(conn, "Failed to open data file");
        PQclear(PQgetResult(conn));
        return false;
    }

    qDebug() << "[PGCopy] Opened temp file for COPY streaming:" << dfd->copy_temp_filename;

    /* Stream data line-by-line from temp file */
    char buffer[65536];  /* 64KB buffer for reading lines */
    int lineCount = 0;
    bool streamError = false;

    while (fgets(buffer, sizeof(buffer), tempFile) != NULL) {
        int len = strlen(buffer);

        /* Send line to PostgreSQL COPY */
        if (PQputCopyData(conn, buffer, len) == -1) {
            qWarning() << "[PGCopy] PQputCopyData failed:" << PQerrorMessage(conn);
            streamError = true;
            break;
        }

        lineCount++;
        if (lineCount % 10000 == 0) {
            qDebug() << "[PGCopy] COPY streamed" << lineCount << "records";
        }
    }

    if (ferror(tempFile)) {
        qWarning() << "[PGCopy] Error reading from temp file during COPY streaming";
        streamError = true;
    }

    fclose(tempFile);

    if (streamError) {
        PQputCopyEnd(conn, "Copy operation interrupted by client");
        PQclear(PQgetResult(conn));
        return false;
    }

    /* End COPY operation */
    if (PQputCopyEnd(conn, NULL) == -1) {
        qWarning() << "[PGCopy] PQputCopyEnd failed:" << PQerrorMessage(conn);
        return false;
    }

    qDebug() << "[PGCopy] COPY stream ended after" << lineCount << "records";

    /* Get and validate result */
    PGresult* res = PQgetResult(conn);
    if (!res) {
        qWarning() << "[PGCopy] PQgetResult returned NULL after COPY";
        return false;
    }

    ExecStatusType status = PQresultStatus(res);
    bool success = (status == PGRES_COMMAND_OK);

    if (!success) {
        qWarning() << "[PGCopy] COPY command failed:" << PQresultErrorMessage(res);
    } else {
        qDebug() << "[PGCopy] COPY command succeeded via libpq";
    }

    PQclear(res);
    return success;
}
#endif /* USE_PQ_API */

/*	Name: sqlCopyViaShell()
 *
 *	Description:
 *		Executes PostgreSQL COPY command using psql shell subprocess.
 *		This approach is faster than libpq direct API but does NOT maintain
 *		transaction isolation (COPY runs in separate connection).
 *
 *		NOTE: This function does NOT maintain table locks acquired on the
 *		main Qt connection. The COPY executes asynchronously in a separate
 *		psql process, defeating transaction isolation. Use #ifdef USE_PQ
 *		to switch to sqlCopyViaLibpq() if transaction safety is required.
 *
 *	ALGORITHM:
 *		1. Build psql command with hardcoded credentials
 *		2. Pipe temp file to psql via shell
 *		3. Execute via system() call
 *		4. Check exit code for success/failure
 *
 *	Arguments:
 *		dfd (DFILE*) - Open data file with temp filename and record count (IN)
 *		copyCmd (QString&) - Complete COPY command to execute (IN)
 *
 *	Return Values:
 *		bool - TRUE if COPY succeeded, FALSE if COPY failed or error occurred
 *
 *	Author: John Gilreath
 *	Date  : December 12, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  12/12/2025	jgilreath	Shell-based implementation (faster than libpq)
 *
 *	PERFORMANCE NOTE: Shell approach is ~5x faster than libpq direct API
 *	for streaming 170K+ records, likely due to OS-level buffering optimization.
 *
 *	LOCKING NOTE: This approach does NOT preserve table locks from the Qt
 *	transaction because psql runs in a separate connection. See #ifdef USE_PQ
 *	for transaction-safe libpq alternative.
 */
static bool sqlCopyViaShell(DFILE* dfd, const QString& copyCmd)
{
    if (!dfd || copyCmd.isEmpty()) {
        qWarning() << "[PGCopy] Invalid parameters to sqlCopyViaShell()";
        return false;
    }

    /* Build psql command using pg_service connection profile */
    /* Pipe temp file to psql via stdin */
    QString psqlCmd = QString("cat %1 | psql service=ezc -c \"%2\"")
        .arg(dfd->copy_temp_filename)
        .arg(copyCmd);

    qDebug() << "[PGCopy] Executing COPY via psql shell (subprocess)";

    /* Execute via system - psql will use temp file piped to stdin */
    int ret = system(psqlCmd.toUtf8().constData());

    if (ret != 0) {
        qWarning() << "[PGCopy] psql COPY command failed with exit code:" << ret;
        return false;
    }

    qDebug() << "[PGCopy] COPY command succeeded via psql";
    return true;
}

/*	Name: sqlBegin()
 *
 *	Description:
 *		Starts a transaction for batched insert operations.
 *		Called from sql_write_hook() on the first write when batch mode is detected.
 *		Ensures all operations within a batch are within a single transaction.
 *		Safe to call multiple times - uses flag to protect against duplicate BEGINs.
 *
 *	ALGORITHM:
 *		1. Validate DFILE pointer
 *		2. Check if transaction already started (skip if already in transaction)
 *		3. Mark transaction as started
 *		4. Get database connection
 *		5. Execute BEGIN transaction
 *		6. Log the operation
 *
 *	Arguments:
 *		dfd (DFILE*) - Open data file with batch mode enabled (IN/OUT)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : December 12, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  12/12/2025	jgilreath	Initial implementation to start transactions at dopen() time
 *  01/09/2026	claude		Refactored to start on first write, removed batch_mode check
 */
static void sqlBegin(DFILE* dfd)
{
    if (!dfd) {
        qWarning() << "[sqlBegin] Invalid DFILE";
        return;
    }

    /* Skip if transaction already started */
    if (dfd->batch_transaction_started) {
        qDebug() << "[sqlBegin] Transaction already started for file:" << dfd->filename;
        return;
    }

    /* Mark transaction as started */
    qDebug() << "[sqlBegin] BEGIN transaction started for file:" << dfd->filename
             << "timestamp=" << QDateTime::currentMSecsSinceEpoch();

    /* Get database connection */
    QSqlDatabase db = getDatabase();
    if (!db.isOpen()) {
        qWarning() << "[sqlBegin] Database connection not available";
        return;
    }

    /* Execute BEGIN transaction */
    QSqlQuery q(db);
    if (!q.exec("BEGIN")) {
        qWarning() << "[sqlBegin] BEGIN FAILED:" << q.lastError().text()
                   << "file=" << dfd->filename;
        return;
    }

    dfd->batch_transaction_started = TRUE;

}

/*	Name: sqlCommit()
 *
 *	Description:
 *		Explicitly commits an active batch transaction and resets counters.
 *		Called automatically by dclose(), but can be used for explicit control.
 *
 *	ALGORITHM:
 *		If batch_transaction_started is TRUE:
 *			Execute COMMIT
 *			Reset batch_transaction_started to FALSE
 *			Log operation
 *		Otherwise, log that no transaction was active
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		dfd (DFILE*)	Open data file with active batch transaction (IN)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : November 26, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  11/26/2025	jgilreath	Initial implementation for batch commit
 *  11/26/2025	jgilreath	Renamed from sql_batch_commit to sqlCommit
 */
static void sqlCommit(DFILE* dfd)
{
    qDebug() << "[sqlCommit] Entering commit function";

    if (!dfd) {
        qWarning() << "Invalid DFILE passed to sqlCommit()";
        return;
    }

    /* Handle Postgres Copy execution (detection happened in sqlBegin() at dopen() time) */
    if (dfd->copy_mode) {
        qDebug() << "[sqlCommit] Executing COPY FROM STDIN for" << dfd->copy_record_count << "records";

        /* Close temp file so we can read it */
        if (dfd->copy_temp_file) {
            fclose(dfd->copy_temp_file);
            dfd->copy_temp_file = NULL;
        }

        /* We must issue a system call to redirect our load file to Postgres's
         * COPY functionality */

        QString copyCmd = buildCopyCommand(dfd);
        if (copyCmd.isEmpty()) {
            qWarning() << "[sqlCommit] Failed to build COPY command";
            if (dfd->copy_temp_filename) {
                unlink(dfd->copy_temp_filename);
                free(dfd->copy_temp_filename);
                dfd->copy_temp_filename = NULL;
            }
            return;
        }

        QSqlDatabase db = getDatabase();
        if (!db.isOpen()) {
            qWarning() << "[sqlCommit] Database connection not available for COPY";
            if (dfd->copy_temp_filename) {
                unlink(dfd->copy_temp_filename);
                free(dfd->copy_temp_filename);
                dfd->copy_temp_filename = NULL;
            }
            return;
        }

        /* Execute COPY via selected transport method */
        long startTimeMs = QDateTime::currentMSecsSinceEpoch();
        qDebug() << "[sqlCommit] COPY start time:" << startTimeMs << "ms";

#ifdef USE_PQ_API
        bool copySuccess = sqlCopyViaLibpq(dfd, copyCmd);
        qDebug() << "[sqlCommit] Using libpq transport (transaction-safe, slower)";
#else
        bool copySuccess = sqlCopyViaShell(dfd, copyCmd);
        qDebug() << "[sqlCommit] Using psql shell transport (faster, separate connection)";
#endif

        long endTimeMs = QDateTime::currentMSecsSinceEpoch();
        long elapsedMs = endTimeMs - startTimeMs;
        qDebug() << "[sqlCommit] COPY end time:" << endTimeMs << "ms (elapsed:" << elapsedMs << "ms)";

        if (!copySuccess) {
            qWarning() << "[sqlCommit] COPY command failed";

            QSqlQuery q(db);
            if (!q.exec("ROLLBACK")) {
                qWarning() << "[sqlCommit] ROLLBACK failed:" << q.lastError().text();
                // Close connection to force clean reconnection
                SqlCommon::closeDatabase();
            }

            if (dfd->copy_temp_filename) {
                unlink(dfd->copy_temp_filename);
                free(dfd->copy_temp_filename);
                dfd->copy_temp_filename = NULL;
            }
            return;
        }

        qDebug() << "[sqlCommit] COPY completed";

        /* Commit transaction */
        QSqlQuery q(db);
        if (!q.exec("COMMIT")) {
            qWarning() << "[sqlCommit] COMMIT failed after COPY:" << q.lastError().text();
            if (dfd->copy_temp_filename) {
                unlink(dfd->copy_temp_filename);
                free(dfd->copy_temp_filename);
                dfd->copy_temp_filename = NULL;
            }
            return;
        }

        qDebug() << "[sqlCommit] COPY FROM STDIN completed successfully for"
                 << dfd->copy_record_count << "records into" << dfd->filename;

        /* Clean up temp file */
        if (dfd->copy_temp_filename) {
            if (unlink(dfd->copy_temp_filename) == 0) {
                qDebug() << "[sqlCommit] Deleted temp file:" << dfd->copy_temp_filename;
            } else {
                qWarning() << "[sqlCommit] Failed to delete temp file:" << dfd->copy_temp_filename;
            }
            free(dfd->copy_temp_filename);
            dfd->copy_temp_filename = NULL;
        }

        return;
    }

    /* Handle Batched INSERT logic */
    if (!dfd->batch_transaction_started) {
        qDebug() << "[sqlCommit] No active transaction to commit for file:" << dfd->filename;
        return;
    }

    /* Ensure that all data in batch_buffer is inserted before transaction closes */
    if (dfd->batch_insert_count > 0 && dfd->batch_buffer) {
        qDebug() << "[sqlCommit] Flushing remaining batch records BEFORE commit"
                 << "count=" << dfd->batch_insert_count
                 << "buffer_size=" << dfd->batch_buffer_size;
        sqlFlushInsertBatch(dfd);
    }

    QSqlDatabase db = getDatabase();
    if (!db.isOpen()) {
        qWarning() << "[sqlCommit] Database connection not available";
        return;
    }

    QSqlQuery q(db);

    if (!q.exec("COMMIT")) {
        qWarning() << "[sqlCommit] COMMIT FAILED:" << q.lastError().text()
                   << "file=" << dfd->filename;

        QSqlQuery q(db);
        if (!q.exec("ROLLBACK")) {
            qWarning() << "[sqlCommit] ROLLBACK after failed COMMIT also failed:" 
                   << q.lastError().text();
            // Close connection to force clean reconnection
            SqlCommon::closeDatabase();
        }
        return;
    }

    qDebug() << "[sqlCommit] COMMIT SUCCEEDED"
             << "timestamp=" << QDateTime::currentMSecsSinceEpoch();

    dfd->batch_transaction_started = FALSE;
}

/*	Name: sqlRollback()
 *
 *	Description:
 *		Explicitly rolls back an active batch transaction and resets counters.
 *		Called from sql_rollback() hook function to perform the actual rollback operation.
 *
 *	ALGORITHM:
 *		If batch_transaction_started is TRUE:
 *			Execute ROLLBACK
 *			Reset batch_transaction_started to FALSE
 *			Log operation
 *		Otherwise, log that no transaction was active
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		dfd (DFILE*)	Open data file with active batch transaction (IN)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : November 26, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  11/26/2025	jgilreath	Initial implementation for batch rollback
 *  01/06/2026	jgilreath	Refactored to match sqlCommit() pattern: renamed to sqlRollback()
 */
static void sqlRollback(DFILE* dfd)
{
    qDebug() << "[sqlRollback] Entering rollback function";

    if (!dfd) {
        qWarning() << "Invalid DFILE passed to sqlRollback()";
        return;
    }

    if (!dfd->batch_transaction_started) {
        qDebug() << "[sqlRollback] No active batch transaction to rollback for file:" << dfd->filename;
        return;
    }

    QSqlDatabase db = getDatabase();
    if (!db.isOpen()) {
        qWarning() << "[sqlRollback] Database connection not available for batch rollback";
        return;
    }

    QSqlQuery q(db);
    if (!q.exec("ROLLBACK")) {
        qWarning() << "[sqlRollback] ROLLBACK FAILED:" << q.lastError().text()
                   << "file=" << dfd->filename;
        return;
    }

    qDebug() << "[sqlRollback] ROLLBACK SUCCEEDED"
             << "file=" << dfd->filename
             << "timestamp=" << QDateTime::currentMSecsSinceEpoch();

    dfd->batch_transaction_started = FALSE;
}	/* sqlRollback() */

/*	Name: sql_rollback_hook()
 *
 *	Description:
 *		Hook function for rolling back batch transactions explicitly.
 *		This hook follows the naming convention of other SQL hooks in the library
 *		(sql_write_hook, sql_commit_hook, sql_delete_hook, etc.).
 *
 *	ALGORITHM:
 *		Validate DFILE pointer
 *		Call sqlRollback() to perform the actual rollback operation
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		dfd (DFILE*)	Open data file with active batch transaction (IN)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : January 6, 2026
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  01/06/2026	jgilreath	Refactored from sql_batch_rollback(), follows sql_commit_hook() pattern
 *  01/06/2026	jgilreath	Renamed to sql_rollback_hook() for consistency with other hooks
 */
extern "C" void sql_rollback_hook(DFILE* dfd)
{
    if (!dfd) {
        qWarning() << "Invalid DFILE passed to sql_rollback_hook()";
        return;
    }

    if (!dfd->is_sql_active) {
        return;  // SQL not active for this datafile
    }

    sqlRollback(dfd);
}

/*	Name: sql_commit_hook()
 *
 *	Description:
 *		Hook function for auto-committing batch transactions from dclose().
 *		This hook is called when a DFILE is closed and has an active batch
 *		transaction pending. It follows the naming convention of other SQL
 *		hooks in the library (sql_write_hook, sql_delete_hook, etc.).
 *
 *	ALGORITHM:
 *		Validate DFILE pointer
 *		Call sqlCommit() to perform the actual commit operation
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		dfd (DFILE*)	Open data file with active batch transaction (IN)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : November 26, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  11/26/2025	jgilreath	Initial implementation as commit hook for dclose()
 */

extern "C" void sql_commit_hook(DFILE* dfd)
{
    if (!dfd) {
        qWarning() << "Invalid DFILE passed to sql_commit_hook()";
        return;
    }

    if (!dfd->is_sql_active) {
        return;  // SQL not active for this datafile
    }

    sqlCommit(dfd);
}

/*	Name: getCurrentTimeMs()
 *
 *	Description:
 *		Returns current time in milliseconds since epoch.
 *		Used for timeout-based batch flushing.
 *
 *	ALGORITHM:
 *		Call QDateTime::currentMSecsSinceEpoch() and return value
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		NONE
 *
 *	Return Values:
 *		long - Milliseconds since epoch
 *
 *	Author: John Gilreath
 *	Date  : December  4, 2025
 *
 *  Modification History:
 *  12/04/2025	jgilreath	Initial implementation for batch timeout
 */
static long getCurrentTimeMs(void)
{
    return QDateTime::currentMSecsSinceEpoch();
}

/*	Name: escapeSqlValue()
 *
 *	Description:
 *		Escapes and quotes a SQL string value.
 *		Handles single quotes and backslashes.
 *
 *	ALGORITHM:
 *		Take input string
 *		Replace single quotes with two single quotes (SQL standard)
 *		Wrap in single quotes
 *		Return escaped string
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		value (const QString&) - Unescaped value (IN)
 *
 *	Return Values:
 *		QString - Escaped and quoted value suitable for SQL
 *
 *	Author: John Gilreath
 *	Date  : December  4, 2025
 *
 *  Modification History:
 *  12/04/2025	jgilreath	Initial implementation for SQL injection prevention
 */
static QString escapeSqlValue(const QString& value)
{
    /* Replace single quotes with two single quotes (SQL escaping) */
    QString escaped = value;
    escaped.replace("'", "''");

    /* Wrap in single quotes */
    return "'" + escaped + "'";
}

/*	Name: appendToBatchBuffer()
 *
 *	Description:
 *		Appends a VALUES tuple to the batch buffer.
 *		Manages dynamic buffer resizing and concatenation.
 *
 *	ALGORITHM:
 *		Calculate new size (current + tuple + comma separator)
 *		If new size exceeds capacity:
 *			Reallocate buffer with geometric growth (2x)
 *			Cap at getBatchBufferMaxSize()
 *			If reallocation fails, trigger immediate flush
 *		If not first tuple, append comma separator
 *		Append VALUES tuple to buffer
 *		Update size counter
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		dfd (DFILE*) - Open data file with batch buffer (IN/OUT)
 *		valuesTuple (const QString&) - VALUES tuple like "(val1, val2, ...)" (IN)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : December  4, 2025
 *
 *  Modification History:
 *  12/04/2025	jgilreath	Initial implementation for batch buffer management
 */
static void appendToBatchBuffer(DFILE* dfd, const QString& valuesTuple)
{
    if (!dfd || !dfd->batch_buffer) {
        return;
    }

    /* Convert QString to C string for buffer operations */
    QByteArray ba = valuesTuple.toUtf8();
    int tupleLen = ba.length();

    /* Account for comma separator if not first tuple */
    int separatorLen = (dfd->batch_insert_count > 0) ? 1 : 0;  /* comma */
    int newSize = dfd->batch_buffer_size + separatorLen + tupleLen;

    /* Check if we need to resize */
    while (newSize > dfd->batch_buffer_capacity) {
        int newCapacity = dfd->batch_buffer_capacity * 2;

        /* Cap at maximum size */
        if (newCapacity > getBatchBufferMaxSize()) {
            newCapacity = getBatchBufferMaxSize();
        }

        /* If still too small, trigger flush */
        if (newSize > newCapacity) {
            qDebug() << "Buffer overflow detected, flushing batch"
                     << "needed=" << newSize
                     << "capacity=" << newCapacity;
            sqlFlushInsertBatch(dfd);
            newSize = tupleLen;  /* Reset size to just the new tuple */
            separatorLen = 0;    /* No separator for first tuple after flush */
            break;
        }

        /* Reallocate buffer */
        char* newBuffer = (char*)realloc(dfd->batch_buffer, newCapacity);
        if (newBuffer == NULL) {
            qWarning() << "Failed to reallocate batch buffer to size:"
                       << newCapacity;
            free(dfd->batch_buffer);
            newBuffer = (char*)malloc(getBatchBufferInitialSize());
            if (newBuffer == NULL) {
                qWarning() << "CRITICAL: Cannot allocate batch buffer";
                dfd->batch_buffer = NULL;
                dfd->batch_mode = FALSE;  /* Disable batching */
                return;
            }
            dfd->batch_buffer = newBuffer;
            dfd->batch_buffer_capacity = getBatchBufferInitialSize();
            dfd->batch_buffer_size = 0;
            newSize = tupleLen;
            separatorLen = 0;
            break;
        }

        dfd->batch_buffer = newBuffer;
        dfd->batch_buffer_capacity = newCapacity;
        qDebug() << "Batch buffer resized to:" << newCapacity << "bytes";
    }

    /* Append separator if not first tuple */
    if (separatorLen > 0) {
        dfd->batch_buffer[dfd->batch_buffer_size] = ',';
        dfd->batch_buffer_size++;
    }

    /* Append the VALUES tuple */
    memcpy(dfd->batch_buffer + dfd->batch_buffer_size, ba.data(), tupleLen);
    dfd->batch_buffer_size += tupleLen;
    dfd->batch_buffer[dfd->batch_buffer_size] = '\0';  /* Null terminate */
}

/*	Name: shouldFlushBatch()
 *
 *	Description:
 *		Determines if batch should be flushed based on three triggers:
 *		1. Size limit (100 records)
 *		2. Buffer limit (1MB)
 *		3. Timeout (1 second)
 *
 *	ALGORITHM:
 *		If batch_insert_count >= getBatchInsertSize(): return TRUE (size limit trigger)
 *		If batch_buffer_size >= getBatchBufferMaxSize(): return TRUE (buffer limit trigger)
 *		If time since batch_last_flush_time >= getBatchInsertTimeoutMs(): return TRUE (timeout trigger)
 *		Otherwise: return FALSE
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		dfd (DFILE*) - Open data file with batch state (IN)
 *
 *	Return Values:
 *		bool - TRUE if batch should be flushed, FALSE otherwise
 *
 *	Author: John Gilreath
 *	Date  : December  4, 2025
 *
 *  Modification History:
 *  12/04/2025	jgilreath	Initial implementation for batch flushing triggers
 */

/*	Name: useBatchMode()
 *
 *	Description:
 *		Determines whether batch mode should be used for INSERT operations.
 *		Batch mode is enabled if EITHER:
 *		1. The DF_BATCH flag is explicitly set in the DFILE structure, OR
 *		2. The calling program is in the list of batch-mode programs (mkdf, reorg)
 *
 *		This allows utility programs (mkdf, reorg) to automatically benefit from
 *		batch mode optimization without requiring explicit flag configuration,
 *		while maintaining backward compatibility with code that sets the flag.
 *
 *	Algorithm:
 *		1. Check if dfd->batch_mode flag is set (DF_BATCH)
 *		2. If not, detect calling program name from /proc/self/comm
 *		3. Check if program name is in batch programs list (mkdf, reorg)
 *		4. Return TRUE if EITHER condition is satisfied
 *
 *	Arguments:
 *		dfd (DFILE*) - Open data file with batch_mode flag (IN)
 *
 *	Return Values:
 *		true  - Use batch mode
 *		false - Use single INSERT per record
 *
 *	Author: Claude Code
 *	Date  : January 9, 2026
 *
 *  Modification History:
 *  01/09/2026	claude		Added caller-based batch mode detection
 */
static bool useBatchMode(DFILE* dfd)
{
    /* Check explicit batch mode flag first */
    if (dfd && dfd->batch_mode == TRUE) {
        return true;
    }

    /* Check if calling program is in batch programs list */
    static bool initialized = false;
    static bool is_batch_program = false;

    if (!initialized) {
        char progname[256];
        char *ptr, *base;
        FILE *fp;

        is_batch_program = false;

        /* Read program name from /proc/self/comm (Linux standard) */
        fp = fopen("/proc/self/comm", "r");
        if (fp != NULL) {
            if (fgets(progname, sizeof(progname), fp) != NULL) {
                /* Remove trailing newline */
                size_t len = strlen(progname);
                if (len > 0 && progname[len - 1] == '\n') {
                    progname[len - 1] = '\0';
                }

                /* Extract basename in case full path is provided */
                base = strrchr(progname, '/');
                ptr = (base != NULL) ? base + 1 : progname;

                /* List of programs that use batch mode */
                QStringList batchPrograms;
                batchPrograms << "mkdf" << "reorg";

                /* Walk table to check if program is in batch list */
                if (batchPrograms.contains(ptr)) {
                    is_batch_program = true;
                    qDebug() << "Batch mode enabled for program:" << ptr;
                }
            }
            fclose(fp);
        }

        initialized = true;
    }

    return is_batch_program;
}	/* useBatchMode() */

static bool shouldFlushBatch(DFILE* dfd)
{
    if (!dfd) {
        return false;
    }

    /* Check size limit: 100 records */
    if (dfd->batch_insert_count >= getBatchInsertSize()) {
        qDebug() << "Flush trigger: Size limit reached"
                 << "records=" << dfd->batch_insert_count
                 << "limit=" << getBatchInsertSize();
        return true;
    }

    /* Check buffer limit: 1MB */
    if (dfd->batch_buffer_size >= getBatchBufferMaxSize()) {
        qDebug() << "Flush trigger: Buffer limit reached"
                 << "size=" << dfd->batch_buffer_size
                 << "limit=" << getBatchBufferMaxSize();
        return true;
    }

    /* Check timeout: 1 second */
    long currentTime = getCurrentTimeMs();
    if (dfd->batch_insert_count > 0 &&
        (currentTime - dfd->batch_last_flush_time) >= getBatchInsertTimeoutMs()) {
        qDebug() << "Flush trigger: Timeout reached"
                 << "elapsed=" << (currentTime - dfd->batch_last_flush_time) << "ms"
                 << "timeout=" << getBatchInsertTimeoutMs() << "ms";
        return true;
    }

    return false;
}

/*	Name: sqlFlushInsertBatch()
 *
 *	Description:
 *		Executes accumulated batch as single INSERT statement.
 *		Builds multi-row INSERT and executes within transaction.
 *
 *	ALGORITHM:
 *		If no records accumulated, return
 *		Build INSERT statement: INSERT INTO table (cols) VALUES (tuples)
 *		Execute within transaction boundary
 *		If successful, reset batch counters and clear buffer
 *		If failure, log error
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		dfd (DFILE*) - Open data file with batch buffer (IN/OUT)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : December  4, 2025
 *
 *  Modification History:
 *  12/04/2025	jgilreath	Initial implementation for multi-row INSERT execution
 */
static void sqlFlushInsertBatch(DFILE* dfd)
{
    if (!dfd || dfd->batch_insert_count == 0) {
        return;
    }

    qDebug() << "Flushing accumulated batch:" << dfd->batch_insert_count
             << "records, buffer size:" << dfd->batch_buffer_size;

    /* Get database handle */
    QSqlDatabase db = getDatabase();
    if (!db.isOpen()) {
        qWarning() << "Database connection not available for flush";
        return;
    }

    /* Get column names from table */
    QString tableName = QString(dfd->filename).remove(".df");
    QStringList tableColumns = getTableColumns(tableName);
    if (tableColumns.isEmpty()) {
        qWarning() << "Could not retrieve columns for table:" << tableName;
        return;
    }

    /* Build column list in table schema order (always consistent across formats) */
    /* Column order: id, format_type, [directly mapped fields], attributes */
    QStringList columnList;
    columnList << "id" << "format_type";

    /* Add all other columns in table schema order */
    for (const QString& col : tableColumns) {
        if (col != "id" && col != "format_type") {
            columnList << col;
        }
    }

    QString columnListStr = columnList.join(", ");
    qDebug() << "Building INSERT with columns:" << columnListStr;

    /* Build multi-row INSERT statement */
    QString insertSql = QString("INSERT INTO %1 (").arg(tableName);
    insertSql += columnListStr;

    insertSql += ") VALUES ";

    /* Add accumulated VALUES tuples from batch buffer */
    if (dfd->batch_buffer && dfd->batch_buffer_size > 0) {
        QString bufferContent = QString::fromUtf8(dfd->batch_buffer, dfd->batch_buffer_size);
        insertSql += bufferContent;
    }

    qDebug() << "Generated INSERT SQL:"
             << insertSql.left(200) << "... (truncated)";
    qDebug() << "Full SQL length:" << insertSql.length() << "characters";

    /* Execute the batched INSERT */
    QSqlQuery insertQuery(db);
    if (!insertQuery.exec(insertSql)) {
        qWarning() << "Batched INSERT failed:" << insertQuery.lastError().text();
        qWarning() << "SQL was:" << insertSql.left(500) << "...";

        QSqlQuery q(db);
        if (!q.exec("ROLLBACK")) {
            qWarning() << "Batched INSERT ROLLBACK failed:" << q.lastError().text();
        SqlCommon::closeDatabase();
        }
        return;
    }

    qDebug() << "Successfully executed batched INSERT for" << dfd->batch_insert_count
             << "records into table" << tableName;

    /* Reset batch counters and clear buffer */
    dfd->batch_insert_count = 0;
    dfd->batch_buffer_size = 0;
    if (dfd->batch_buffer) {
        memset(dfd->batch_buffer, 0, dfd->batch_buffer_capacity);
    }
    dfd->batch_last_flush_time = getCurrentTimeMs();
}

/*	Name: accumulateInsertValues()
 *
 *	Description:
 *		Accumulates a single record's VALUES tuple in batch buffer.
 *		Prepares record data for batch INSERT execution.
 *
 *	ALGORITHM:
 *		Extract filename, format number, record buffer from DFILE
 *		Look up EZCMD metadata for this table/format
 *		Build VALUES tuple string with escaped values
 *		Append to batch buffer via appendToBatchBuffer()
 *		Increment batch_insert_count
 *		Update batch_last_flush_time if first tuple
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		dfd (DFILE*) - Open data file with current record (IN/OUT)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : December  4, 2025
 *
 *  Modification History:
 *  12/04/2025	jgilreath	Initial implementation for batch record accumulation
 */
static void accumulateInsertValues(DFILE* dfd)
{
    if (!dfd || !dfd->crec || !dfd->batch_buffer) {
        qWarning() << "Invalid DFILE or batch buffer in accumulateInsertValues()";
        return;
    }

    /* Extract table data (columns, values, bindings) from DFILE record */
    TableData td = getTableDataFromDfile(dfd, DF_UPDATE);
    if (td.columns.isEmpty()) {
        qWarning() << "Failed to extract table data for:" << dfd->filename;
        return;
    }

    /* Build VALUES tuple with escaped values: (val1, val2, val3, ...) */
    QString valuesTuple = "(";
    for (int i = 0; i < td.values.count(); i++) {
        if (i > 0) {
            valuesTuple += ", ";
        }

        const QString& placeholder = td.values[i];
        if (placeholder == "NULL") {
            valuesTuple += "NULL";
        } else {
            QString paramName = placeholder.mid(1);  /* Remove leading ':' */
            if (td.bindings.contains(paramName)) {
                QString value = td.bindings[paramName];
                /* For numeric values (id, format_type), don't quote */
                if (paramName == "id" || paramName == "format_type") {
                    valuesTuple += value;
                } else {
                    /* For string values, escape and quote */
                    valuesTuple += escapeSqlValue(value);
                }
            } else {
                qWarning() << "Missing bind value for parameter:" << paramName;
                valuesTuple += "NULL";
            }
        }
    }
    valuesTuple += ")";

    /* Append to batch buffer */
    appendToBatchBuffer(dfd, valuesTuple);

    /* Update timestamp on first tuple */
    if (dfd->batch_insert_count == 0) {
        dfd->batch_last_flush_time = getCurrentTimeMs();
    }

    /* NOTE: batch_insert_count is incremented by sql_write_hook() after this function */
}

/*	Name: generateTempFilename()
 *
 *	Description:
 *		Generates a unique temporary filename for Postgres COPY staging files.
 *		Uses hostname, PID, and timestamp to ensure uniqueness.
 *
 *	Arguments:
 *		NONE
 *
 *	Return Values:
 *		char* - Pointer to static buffer containing temp filename
 *
 *	Author: John Gilreath
 *	Date  : December 10, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  12/10/2025	jgilreath	Initial implementation
 */
static char* generateTempFilename(void)
{
    static char tempFilename[256];
    char hostname[64];

    /* Get hostname (fallback to 'unknown' if fails) */
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strncpy(hostname, "unknown", sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
    }

    /* Build temp filename: $ETROOT/tmp/.ezc_copy_<hostname>_<PID>_<timestamp>.txt */
    char tempDir[256];
    snprintf(tempDir, sizeof(tempDir), "%s/tmp", my_etroot());

    snprintf(tempFilename, sizeof(tempFilename),
             "%s/%s%s_%d_%ld.txt",
             tempDir,
             COPY_TEMP_PREFIX,
             hostname,
             getpid(),
             time(NULL));

    return tempFilename;
}

/*	Name: sqlWrite()
 *
 *	Description:
 *		Converts an EZ-C record to a PostgreSQL operation using metadata.
 *		Supports upsert (INSERT ON CONFLICT DO UPDATE) and DELETE operations.
 *		For upsert: Extracts field values from the EZ-C record buffer, maps them to
 *		PostgreSQL columns, and handles unmapped fields via JSONB storage.
 *		For DELETE: Removes the corresponding row from PostgreSQL using the record ID.
 *
 *	ALGORITHM:
 *		Validate operation type (DF_UPDATE or DF_DELETE)
 *		For DELETE operation:
 *			Extract filename and DFILE seek position (record ID) from DFILE
 *			Get database handle
 *			Build parameterized DELETE statement using record ID
 *			Execute DELETE
 *			Return
 *		For DF_UPDATE (upsert) operations:
 *			Extract filename, format number, and record buffer from DFILE
 *          Get information for associated Postgres table
 *			Execute INSERT ... ON CONFLICT DO UPDATE
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		dfd (const DFILE*)	Open data file with current record (IN)
 *		operation (int)		DF_UPDATE (upsert) or DF_DELETE (IN)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : November  7, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  11/14/2025	jgilreath	Renamed from sqlInsert(), added operation parameter
 *  11/19/2025	jgilreath	Added support for DF_DELETE operations
 */
static void sqlWrite(const DFILE* dfd, int operation)
{
    // Validate DFILE structure
    if (!dfd) {
        qWarning() << "Invalid DFILE structure";
        return;
    }

    // For DELETE operations, we don't need crec (current record)
    // For INSERT/UPDATE operations, we do need crec
    if ((operation != DF_DELETE) && !dfd->crec) {
        qWarning() << "Invalid DFILE structure - no current record for operation:" << operation;
        return;
    }

    // Validate operation type
    if (operation != DF_UPDATE && operation != DF_DELETE) {
        qWarning() << "Invalid operation type in sqlWrite():" << operation;
        return;
    }

    // Extract the filename from DFILE
    const char* dfileName = dfd->filename;
    qDebug() << "sqlWrite: operation=" << (operation == DF_UPDATE ? "UPDATE" : "DELETE")
             << "dfileName=" << dfileName;
    QString tableName = QString(dfileName).remove(".df");

    // Get database handle (needed for all operations)
    QSqlDatabase db = getDatabase();
    if (!db.isOpen()) {
        qWarning() << "Database connection not available";
        return;
    }

    // Handle DELETE operation separately - doesn't need metadata or record data
    if (operation == DF_DELETE) {
        // For DELETE, we only need the record ID (DFILE seek position)
        qint64 recordId = dfd->seekp;

        qDebug() << "Executing DELETE for" << tableName << "with id=" << recordId;

        // Build and execute DELETE statement
        QSqlQuery deleteQuery(db);
        QString deleteSql = QString("DELETE FROM %1 WHERE id = :id").arg(tableName);

        deleteQuery.prepare(deleteSql);
        deleteQuery.bindValue(":id", recordId);

        qDebug() << "Generated SQL:" << deleteSql;

        if (!deleteQuery.exec()) {
            qWarning() << "DELETE failed:" << deleteQuery.lastError().text();
            qWarning() << "Query was:" << deleteQuery.executedQuery();
            return;
        }

        qDebug() << "Successfully deleted record from" << tableName
                 << "with id=" << recordId;
        return;  // DELETE operation complete
    }

    // Extract table data (columns, values, bindings) from DFILE record
    TableData td = getTableDataFromDfile(dfd, operation);
    if (td.columns.isEmpty()) {
        qWarning() << "Failed to extract table data for:" << dfileName;
        return;
    }

    qDebug() << "Table columns:" << td.columns;

    // Build ON CONFLICT DO UPDATE SET clauses
    // Note: We build these directly without placeholders since column names are not user input
    QStringList setClauses;
    for (int i = 0; i < td.columns.length(); i++) {
        const auto& col = td.columns[i];
        if (col != "id") {  // Don't SET the id column, only use it in WHERE
            if (col == "created_at") {
                // Preserve created_at on update: if existing row has a value, keep it; otherwise use the new value
                // EXCLUDED.created_at will be NOW() for both insert and update since we always set it
                // But on the conflict, we want to preserve the existing created_at if it exists
                setClauses << QString("created_at = COALESCE(%1.created_at, EXCLUDED.created_at)").arg(tableName);
            } else if (td.values[i] != "NULL") {
                setClauses << QString("%1 = EXCLUDED.%1").arg(col);
            } else {
                setClauses << QString("%1 = NULL").arg(col);
            }
        }
    }

    // Build the complete INSERT ... ON CONFLICT DO UPDATE statement
    // We need to escape string values for SQL and build the full statement
    // since the ON CONFLICT DO UPDATE SET with EXCLUDED references doesn't work well with prepared statements in some drivers

    QStringList valueStrings;
    for (int i = 0; i < td.columns.length(); i++) {
        if (td.values[i] == "NULL") {
            valueStrings << "NULL";
        } else {
            const auto& col = td.columns[i];
            if (td.bindings.contains(col)) {
                // Escape single quotes and build the literal value
                QString escapedValue = td.bindings[col];
                escapedValue.replace("'", "''");  // SQL escaping: ' becomes ''
                valueStrings << "'" + escapedValue + "'";
            } else {
                valueStrings << "NULL";
            }
        }
    }

    // Build complete upsert statement with literal values
    QString sql = QString(
        "INSERT INTO %1 (%2) VALUES (%3) "
        "ON CONFLICT (id) DO UPDATE SET %4"
    ).arg(tableName)
     .arg(td.columns.join(", "))
     .arg(valueStrings.join(", "))
     .arg(setClauses.join(", "));

    qDebug() << "Generated SQL (first 500 chars):" << sql.left(500);

    // Execute upsert directly (not using prepared statement)
    // This is safe because column names are not user input
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        qWarning() << "Upsert failed:" << query.lastError().text();
        qWarning() << "Query was (first 500 chars):" << query.executedQuery().left(500);
        return;
    }

    int fmtNum = dfd->crec->recfmt;
    qDebug() << "Successfully upserted record from" << dfileName
             << "fmtNum=" << fmtNum
             << "into" << tableName
             << "timestamp=" << QDateTime::currentMSecsSinceEpoch();
}	/* sqlWrite() */


/*	Name: getTableColumns()
 *
 *	Description:
 *		Retrieves the list of column names for a table in the ezc schema
 *		from PostgreSQL information_schema.columns catalog.
 *		Columns are returned in ordinal position order.
 *
 *	ALGORITHM:
 *		Parse tableName to extract schema and table name
 *		Query information_schema.columns for the table
 *		Filter by table_schema='ezc' and table_name
 *		Order by ordinal_position to maintain column order
 *		Iterate result set and collect column names
 *		Return list of column names
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		tableName (const QString&)	Table name in format "ezc.tablename" (IN)
 *
 *	Return Values:
 *		QStringList	List of column names in ordinal position order
 *					Empty list if table not found or query fails
 *
 *	Author: John Gilreath
 *	Date  : November  7, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 */
static QStringList getTableColumns(const QString& tableName)
{
    static QStringList columnNames;
    static QString currTable;

    // Only run query for new or changed table
    if (currTable == tableName) {
        return columnNames;  // Already queried, return cached result
    }
    currTable = tableName;
    columnNames.clear();

    // Parse tableName to extract the actual table name (strip schema if present)
    QString actualTableName = tableName;
    if (tableName.contains('.')) {
        actualTableName = tableName.split('.').last();
    }

    qDebug() << "getTableColumns: querying columns for table:" << actualTableName;

    // Get database handle
    QSqlDatabase db = getDatabase();
    if (!db.isOpen()) {
        qWarning() << "Database connection not available for getTableColumns";
        return columnNames;  // Return empty list
    }

    // Query information_schema.columns
    QSqlQuery query(db);
    QString sql = 
        "SELECT column_name "
        "FROM information_schema.columns "
        "WHERE table_schema = 'ezc' "
        "  AND table_name = :tableName "
        "ORDER BY ordinal_position";

    query.prepare(sql);
    query.bindValue(":tableName", actualTableName);

    qDebug() << "Executing SQL:" << sql;

    if (!query.exec()) {
        qWarning() << "Failed to query columns for table:" << actualTableName
                   << "Error:" << query.lastError().text();
        return columnNames;  // Return empty list
    }

    // Iterate through result set and collect column names
    while (query.next()) {
        QString columnName = query.value(0).toString();
        columnNames << columnName;
        qDebug() << "  Column:" << columnName;
    }

    qDebug() << "Retrieved" << columnNames.count() << "columns from" << actualTableName;

    return columnNames;
}	/* getTableColumns() */

/*	Name: getTableDataFromDfile()
 *
 *	Description:
 *		Extracts all field values from a DFILE record, isolating all EZ-C/EZCMD logic.
 *		Performs table lookup, schema resolution, metadata extraction, and field mapping.
 *		Returns structured TableData containing columns, values, and bindings map.
 *		High-level callers need not know about EZCMD or EZ-C metadata details.
 *		Handles timestamp column generation for audit trail (created_at, updated_at).
 *
 *	ALGORITHM:
 *		1. Validate DFILE and current record
 *		2. Extract header file name from DFILE filename
 *		3. Retrieve table columns from PostgreSQL schema
 *		4. Look up EZCMD metadata for record format
 *		5. Extract field values from record buffer using EZCMD offsets/lengths
 *		6. Map fields to table columns or accumulate unmapped fields as JSONB
 *		7. Generate current timestamp for audit columns (created_at, updated_at)
 *		8. Build output lists in table schema order
 *		9. Return TableData struct
 *
 *	Arguments:
 *		dfd (const DFILE*) - Open data file with current record (IN)
 *		operation (int) - DF_UPDATE to control timestamp handling (IN, default DF_UPDATE)
 *
 *	Return Values:
 *		TableData - Structure containing columns, values, and bindings map
 *		            (check columns.isEmpty() to determine if extraction succeeded)
 *
 *	Author: John Gilreath
 *	Date  : December 16, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  01/20/2026	claude		Added operation parameter for timestamp handling (created_at, updated_at)
 */
static TableData getTableDataFromDfile(const DFILE* dfd, int operation)
{
    TableData td;

    // Validate DFILE
    if (!dfd || !dfd->crec) {
        qWarning() << "Invalid DFILE in getTableDataFromDfile()";
        return td;
    }

    // Determine the C header associated with this EZ-C datafile
    QString headerFileName = getHeaderFileName(dfd->filename);
    if (headerFileName.isEmpty()) {
        qWarning() << "Could not determine header file name for:" << dfd->filename;
        return td;
    }

    // Get table columns from information schema
    QString tableName = QString(dfd->filename).remove(".df");
    td.columns = getTableColumns(tableName);
    if (td.columns.isEmpty()) {
        qWarning() << "Could not retrieve columns for table:" << tableName;
        return td;
    }

    // Note: created_at is included in all upsert operations.
    // The ON CONFLICT DO UPDATE clause uses COALESCE to preserve created_at
    // for existing records while setting it for new records.

    // Get field names and divisions from EZ-C metadata file
    int fmtNum = dfd->crec->recfmt;
    EZCMD* md = ezcmd_lookup(headerFileName.toUtf8().constData(), fmtNum);
    if (!md) {
        qWarning() << "Metadata not found for header file=" << headerFileName
                   << "fmtNum=" << fmtNum;
        return td;
    }

    const char* ezcData = dfd->crec->databuffer;

    // Generate current timestamp for audit columns (created_at, updated_at)
    QString currentTimestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

    // Build map of column values from EZCMD metadata
    QMap<QString, QString> columnValues;
    QJsonObject unmappedData;

    // Iterate through metadata fields to discover and store values
    for (int i = 0; i < md->count; i++) {
        const char* fieldName = md->fields[i];
        short offset = md->offsets[i];
        short length = md->lengths[i];

        // Skip fields that should be ignored
        QString colName = QString::fromUtf8(fieldName);
        if (SqlCommon::shouldIgnoreField(colName)) {
            continue;
        }

        // Extract field value from record buffer
        QString fieldValue = QString::fromUtf8(ezcData + offset, length);

        // Store in map (direct columns) or JSON (unmapped)
        if (td.columns.contains(colName)) {
            columnValues[colName] = fieldValue;
        } else {
            unmappedData[colName] = fieldValue;
        }
    }

    // Build column and value lists
    for (const QString& col : td.columns) {
        if (col == "id") {
            td.values << ":id";
            td.bindings[col] = QString::number(dfd->seekp);
        } else if (col == "format_type") {
            td.values << ":format_type";
            td.bindings[col] = QString::number(dfd->crec->recfmt);
        } else if (col == "attributes") {
            if (!unmappedData.isEmpty()) {
                td.values << ":attributes";
                QJsonDocument doc(unmappedData);
                td.bindings[col] = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
            } else {
                td.values << "NULL";
            }
        } else if (col == "created_at") {
            td.values << ":created_at";
            td.bindings[col] = currentTimestamp;
        } else if (col == "updated_at") {
            td.values << ":updated_at";
            td.bindings[col] = currentTimestamp;
        } else {
            if (columnValues.contains(col)) {
                td.values << (":" + col);
                td.bindings[col] = columnValues[col];
            } else {
                td.values << "NULL";
            }
        }
    }

    return td;
}	/* getTableDataFromDfile() */


/*	Name: sql_create_hook()
 *
 *	Description:
 *		Hook called from dcreat() after successful EZ-C file creation.
 *		Ensures that the corresponding PostgreSQL table is created fresh and empty,
 *		synchronized with the empty EZ-C file.
 *
 *	ALGORITHM:
 *		Validate filename parameter
 *		Call sqlCreate() to perform the actual table creation
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		basename (const char*)	Basename of the EZ-C file (e.g., "kdlysls") (IN)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : November 17, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 *  11/17/2025	jgilreath	Initial implementation
 */
extern "C" void sql_create_hook(const char* basename)
{
	if (!basename || strlen(basename) == 0) {
		qWarning() << "Invalid basename passed to sql_create_hook()";
		return;
	}

	qDebug() << "sql_create_hook called for:" << basename;

	sqlCreate(basename);
}	/* sql_create_hook() */

/*	Name: sqlCreate()
 *
 *	Description:
 *		Creates a fresh PostgreSQL table for an EZ-C data file.
 *
 *	ALGORITHM:
 *		Extract table name from datafile name
 *		Get ETROOT environment variable (fallback: /user/sms)
 *		Build path to DDL file
 *		Open and read DDL file
 *		Get database connection
 *		Execute DROP TABLE IF EXISTS for the corresponding PostgreSQL table
 *		Execute CREATE TABLE using DDL content
 *
 *	Global Variables:
 *		NONE
 *
 *	Arguments:
 *		dfname (const char*)	EZ-C data file name (IN)
 *
 *	Return Values:
 *		VOID
 *
 *	Author: John Gilreath
 *	Date  : November 17, 2025
 *
 *  Modification History:
 *  MM/DD/CCYY	NAME		DESCRIPTION
 */
static void sqlCreate(const char* dfname)
{
	// Build path to DDL file
    QString tableName = QString(dfname).remove(".df");
    QString ddlPath = QString("%1/files/ezcddls/%2.sql")
        .arg(my_etroot(), tableName);

	// Read DDL file
	QFile ddlFile(ddlPath);
	if (!ddlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
		qWarning() << "Could not open DDL file:" << ddlPath;
		return;
	}

	QString ddlContent = QString::fromUtf8(ddlFile.readAll());
	ddlFile.close();

	if (ddlContent.isEmpty()) {
		qWarning() << "DDL file is empty:" << ddlPath;
		return;
	}

	qDebug() << "Loaded DDL from:" << ddlPath << "bytes:" << ddlContent.length();

	// Get database connection
	QSqlDatabase db = getDatabase();
	if (!db.isOpen()) {
		qWarning() << "Database connection not available for table creation";
		return;
	}

	// Drop existing table if it exists
	QSqlQuery dropQuery(db);
	QString dropSql = QString("DROP TABLE IF EXISTS %1 CASCADE").arg(tableName);

	qDebug() << "Executing DROP TABLE:" << dropSql;

	if (!dropQuery.exec(dropSql)) {
		qWarning() << "DROP TABLE failed for" << tableName
				   << ":" << dropQuery.lastError().text();
	} else {
		qDebug() << "Successfully dropped table (if existed):" << tableName;
	}

	// Create new table from DDL
	QSqlQuery createQuery(db);

	qDebug() << "Executing CREATE TABLE with DDL content";

	if (!createQuery.exec(ddlContent)) {
		qWarning() << "CREATE TABLE failed for" << tableName
				   << ":" << createQuery.lastError().text();
	} else {
		qDebug() << "Successfully created table:" << tableName;
	}

}	/* sqlCreate() */
