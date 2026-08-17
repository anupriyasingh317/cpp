/*
 * sqlCommon.cpp
 * Copyright 2025-2026 AutoZone, Inc.
 *
 * Common SQL database configuration and connection management
 * Used by multiple SQL-based modules in azezc library
 */

#include <QSqlDatabase>
#include <QSqlError>
#include <QSettings>
#include <QCoreApplication>
#include <QDebug>
#include <QString>
#include <QProcessEnvironment>

#include "sqlCommon.h"

namespace SqlCommon {

/* Default batch configuration values */
static const int DEFAULT_BATCH_INSERT_SIZE = 10000;
static const int DEFAULT_BATCH_INSERT_TIMEOUT_MS = 10000;
static const int DEFAULT_BATCH_BUFFER_INITIAL_SIZE = 8192;
static const int DEFAULT_BATCH_BUFFER_MAX_SIZE = 16777216;

/* Global state */
static QCoreApplication* coreApp = nullptr;
static bool initialized = false;
static const QString connectionName = "EZC_CONN";

/* Database configuration cache */
static QString cachedDriver = "QPSQL";
static QString cachedHost = "localhost";
static int cachedPort = 5432;
static QString cachedDatabase = "ezc";
static QString cachedUsername = "ezc";
static QString cachedPassword = "";
static int cachedTimeoutMs = 30000;
static bool databaseConfigLoaded = false;

/* Batch configuration cache */
static int cachedBatchInsertSize = DEFAULT_BATCH_INSERT_SIZE;
static int cachedBatchInsertTimeoutMs = DEFAULT_BATCH_INSERT_TIMEOUT_MS;
static int cachedBatchBufferInitialSize = DEFAULT_BATCH_BUFFER_INITIAL_SIZE;
static int cachedBatchBufferMaxSize = DEFAULT_BATCH_BUFFER_MAX_SIZE;
static bool batchConfigLoaded = false;

/*
 * Helper: Get application root directory
 *
 * Returns ETROOT environment variable or default /opt/sms
 */
static QString getAppRoot()
{
    static QString root;

    if (root.isEmpty()) {
        QString etroot = QProcessEnvironment::systemEnvironment().value("ETROOT");
        if (!etroot.isEmpty()) {
            root = etroot;
        } else {
            root = "/opt/sms";
        }
    }
    return root;
}

/*
 * Helper: Load batch configuration from INI file
 *
 * Loads batch-related parameters from azezc.ini file.
 * Falls back to hardcoded defaults if INI file missing or incomplete.
 */
static void loadBatchConfig()
{
    if (batchConfigLoaded) {
        return;  /* Already loaded, use cached values */
    }

    /* Try to load from INI file */
    QString configPath = QString("%1/etc/azezc.ini").arg(getAppRoot());
    QSettings settings(configPath, QSettings::IniFormat);

    if (settings.status() == QSettings::NoError) {
        cachedBatchInsertSize = settings.value("Performance/batchInsertSize",
                                               DEFAULT_BATCH_INSERT_SIZE).toInt();
        cachedBatchInsertTimeoutMs = settings.value("Performance/batchInsertTimeoutMs",
                                                    DEFAULT_BATCH_INSERT_TIMEOUT_MS).toInt();
        cachedBatchBufferInitialSize = settings.value("Performance/batchBufferInitialSize",
                                                      DEFAULT_BATCH_BUFFER_INITIAL_SIZE).toInt();
        cachedBatchBufferMaxSize = settings.value("Performance/batchBufferMaxSize",
                                                  DEFAULT_BATCH_BUFFER_MAX_SIZE).toInt();
    } else {
        qWarning() << "Could not load batch configuration from" << configPath
                   << "using defaults";
    }

    batchConfigLoaded = true;
}

/*
 * Helper: Load database configuration from INI file
 *
 * Loads configuration from azezc.ini file with environment variable overrides.
 * Only reads INI file once; subsequent calls use cached values (singleton pattern).
 * Parameters are returned via output arguments.
 */
static void loadConfig(
    QString& driver,
    QString& host,
    int& port,
    QString& database,
    QString& username,
    QString& password,
    int& timeoutMs)
{
    /* Check if already loaded */
    if (databaseConfigLoaded) {
        driver = cachedDriver;
        host = cachedHost;
        port = cachedPort;
        database = cachedDatabase;
        username = cachedUsername;
        password = cachedPassword;
        timeoutMs = cachedTimeoutMs;
        return;
    }

    /* Set defaults for first load */
    driver = "QPSQL";
    host = "localhost";
    port = 5432;
    database = "ezc";
    username = "ezc";
    password = "";  /* Never hardcode passwords */
    timeoutMs = 30000;

    /* Try to load from INI file */
    QString configPath = QString("%1/etc/azezc.ini").arg(getAppRoot());
    QSettings settings(configPath, QSettings::IniFormat);

    if (settings.status() == QSettings::NoError) {
        driver = settings.value("Database/driver", driver).toString();
        host = settings.value("Database/host", host).toString();
        port = settings.value("Database/port", port).toInt();
        database = settings.value("Database/database", database).toString();
        username = settings.value("Database/username", username).toString();
        timeoutMs = settings.value("Connection/timeoutMs", timeoutMs).toInt();
    } else {
        qWarning() << "Could not load INI configuration from" << configPath;
    }

    /* Environment variables override INI file values */
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    if (!env.value("DB_HOST").isEmpty()) {
        host = env.value("DB_HOST");
    }
    if (!env.value("DB_PORT").isEmpty()) {
        port = env.value("DB_PORT").toInt();
    }
    if (!env.value("DB_NAME").isEmpty()) {
        database = env.value("DB_NAME");
    }
    if (!env.value("DB_USER").isEmpty()) {
        username = env.value("DB_USER");
    }
    if (!env.value("DB_PASSWORD").isEmpty()) {
        password = env.value("DB_PASSWORD");
    }
    if (!env.value("DB_TIMEOUT_MS").isEmpty()) {
        timeoutMs = env.value("DB_TIMEOUT_MS").toInt();
    }

    /* Cache the loaded values for future calls */
    cachedDriver = driver;
    cachedHost = host;
    cachedPort = port;
    cachedDatabase = database;
    cachedUsername = username;
    cachedPassword = password;
    cachedTimeoutMs = timeoutMs;
    databaseConfigLoaded = true;
}

/*
 * Name: initDatabase()
 *
 * Description:
 *   Initialize SQL database connection from configuration
 *
 *   Loads database parameters from azezc.ini file with environment variable
 *   overrides. Creates Qt database connection if not already established.
 *   Creates QCoreApplication if needed for Qt SQL operations.
 *
 * Algorithm:
 *   1. Create QCoreApplication if none exists (required for Qt SQL)
 *   2. Load configuration from INI file and environment variables
 *   3. Create new database connection with QPSQL driver
 *   4. Set all connection parameters (host, port, database, user, password)
 *   5. Attempt to open connection
 *   6. Log success or failure and return result
 *
 * Return Values:
 *   true if connection established successfully
 *   false if connection failed (caller can check isDatabaseOpen())
 *
 * Author: John Gilreath
 * Date  : February 17, 2026
 *
 * Modification History:
 * MM/DD/CCYY  NAME        DESCRIPTION
 * 02/17/2026  jgilreath   Extracted from sqlWrite.cpp getDatabaseHandle()
 */
bool initDatabase()
{
    /* Ensure QCoreApplication instance exists (required for QSqlDatabase) */
    if (coreApp == nullptr && QCoreApplication::instance() == nullptr) {
        int argc = 0;
        char *argv[] = {};
        coreApp = new QCoreApplication(argc, argv);
        qDebug() << "Created new QCoreApplication instance";
    }

    /* Load configuration */
    QString driver, host, database, username, password;
    int port, timeoutMs;
    loadConfig(driver, host, port, database, username, password, timeoutMs);

    /* Close and remove any existing connection with same name */
    /* Scoped block ensures QSqlDatabase wrapper goes out of scope before removeDatabase() */
    if (QSqlDatabase::contains(connectionName)) {
        {
            QSqlDatabase existingDb = QSqlDatabase::database(connectionName);
            if (existingDb.isOpen()) {
                existingDb.close();
            }
        }  /* existingDb wrapper destroyed here before removeDatabase() call */
        QSqlDatabase::removeDatabase(connectionName);
    }

    /* Create database connection */
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(driver, connectionName);
        db.setHostName(host);
        db.setPort(port);
        db.setDatabaseName(database);
        db.setUserName(username);
        if (!password.isEmpty()) {
            db.setPassword(password);
        }
        db.setConnectOptions(QString("connect_timeout=%1").arg(timeoutMs / 1000));

        qDebug() << "Attempting database connection:"
                 << "Driver:" << driver
                 << "Host:" << host
                 << "Port:" << port
                 << "Database:" << database
                 << "User:" << username;

        /* Attempt to open connection */
        if (!db.open()) {
            qWarning() << "Failed to open database connection";
            qWarning() << "Database error:" << db.lastError().text();
            return false;
        }

        qDebug() << "Successfully opened database connection";
    }  /* db wrapper destroyed here */

    initialized = true;
    return true;
}

/*
 * Name: getDatabase()
 *
 * Description:
 *   Get handle to the active SQL database connection
 *
 *   Implements lazy initialization: if database not yet initialized,
 *   automatically calls initDatabase() to load configuration and open
 *   connection. Returns cached QSqlDatabase handle on subsequent calls.
 *
 * Return Values:
 *   QSqlDatabase handle (caller must check isOpen() to verify validity)
 *
 * Author: John Gilreath
 * Date  : February 17, 2026
 *
 * Modification History:
 * MM/DD/CCYY  NAME        DESCRIPTION
 * 02/17/2026  jgilreath   Extracted from sqlWrite.cpp getDatabaseHandle()
 * 02/17/2026  jgilreath   Added lazy initialization via initDatabase()
 */
QSqlDatabase getDatabase()
{
    /* Lazy initialization: initialize database if not already done */
    if (!initialized) {
        if (!initDatabase()) {
            qWarning() << "Lazy initialization of database failed in getDatabase()";
            return QSqlDatabase();
        }
    }

    /* Check if connection already exists and is open */
    if (QSqlDatabase::contains(connectionName)) {
        QSqlDatabase db = QSqlDatabase::database(connectionName);
        if (db.isOpen()) {
            return db;
        }
    }

    /* Connection not available, return empty/closed handle */
    return QSqlDatabase();
}

/*
 * Name: closeDatabase()
 *
 * Description:
 *   Close database connection and clean up resources
 *
 *   Closes the active database connection and removes it from the
 *   Qt database pool.
 *
 * Return Values:
 *   true if closed successfully
 *   false if connection not registered
 *
 * Author: John Gilreath
 * Date  : February 17, 2026
 *
 * Modification History:
 * MM/DD/CCYY  NAME        DESCRIPTION
 * 02/17/2026  jgilreath   Initial implementation
 */
bool closeDatabase()
{
    if (!QSqlDatabase::contains(connectionName)) {
        return false;
    }

    /* Scoped block ensures QSqlDatabase wrapper is destroyed before removeDatabase() */
    {
        QSqlDatabase db = QSqlDatabase::database(connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }  /* db wrapper destroyed here before removeDatabase() call */

    QSqlDatabase::removeDatabase(connectionName);
    initialized = false;
    qDebug() << "Database connection closed";
    return true;
}

/*
 * Name: isDatabaseOpen()
 *
 * Description:
 *   Check if database connection is currently open
 *
 * Return Values:
 *   true if connection is open and ready for queries
 *   false if connection is closed or invalid
 *
 * Author: John Gilreath
 * Date  : February 17, 2026
 *
 * Modification History:
 * MM/DD/CCYY  NAME        DESCRIPTION
 * 02/17/2026  jgilreath   Initial implementation
 */
bool isDatabaseOpen()
{
    if (!QSqlDatabase::contains(connectionName)) {
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(connectionName);
    return db.isOpen();
}

/*
 * Name: getBatchInsertSize()
 *
 * Description:
 *   Get batch insert size configuration
 *
 * Return Values:
 *   int - batch insert size threshold (number of records)
 *
 * Author: John Gilreath
 * Date  : February 17, 2026
 *
 * Modification History:
 * MM/DD/CCYY  NAME        DESCRIPTION
 * 02/17/2026  jgilreath   Initial implementation
 */
int getBatchInsertSize()
{
    loadBatchConfig();
    return cachedBatchInsertSize;
}

/*
 * Name: getBatchInsertTimeoutMs()
 *
 * Description:
 *   Get batch insert timeout configuration
 *
 * Return Values:
 *   int - timeout in milliseconds
 *
 * Author: John Gilreath
 * Date  : February 17, 2026
 *
 * Modification History:
 * MM/DD/CCYY  NAME        DESCRIPTION
 * 02/17/2026  jgilreath   Initial implementation
 */
int getBatchInsertTimeoutMs()
{
    loadBatchConfig();
    return cachedBatchInsertTimeoutMs;
}

/*
 * Name: getBatchBufferInitialSize()
 *
 * Description:
 *   Get batch buffer initial size configuration
 *
 * Return Values:
 *   int - buffer size in bytes
 *
 * Author: John Gilreath
 * Date  : February 17, 2026
 *
 * Modification History:
 * MM/DD/CCYY  NAME        DESCRIPTION
 * 02/17/2026  jgilreath   Initial implementation
 */
int getBatchBufferInitialSize()
{
    loadBatchConfig();
    return cachedBatchBufferInitialSize;
}

/*
 * Name: getBatchBufferMaxSize()
 *
 * Description:
 *   Get batch buffer maximum size configuration
 *
 * Return Values:
 *   int - maximum buffer size in bytes
 *
 * Author: John Gilreath
 * Date  : February 17, 2026
 *
 * Modification History:
 * MM/DD/CCYY  NAME        DESCRIPTION
 * 02/17/2026  jgilreath   Initial implementation
 */
int getBatchBufferMaxSize()
{
    loadBatchConfig();
    return cachedBatchBufferMaxSize;
}

/*
 * Name: shouldIgnoreField()
 *
 * Description:
 *   Determine if a field should be ignored during SQL table mapping
 *
 *   Filters out internal EZ-C fields and padding that shouldn't be
 *   mapped to database columns:
 *   - Reserved fields (except specific legitimate ones)
 *   - Filler fields (padding/unused space)
 *   - Protect fields (EZ-C specific metadata)
 *
 * Parameters:
 *   fieldName - field name to check
 *
 * Return Values:
 *   true if field should be ignored (skipped)
 *   false if field should be processed
 *
 * Author: John Gilreath
 * Date  : February 27, 2026
 *
 * Modification History:
 * MM/DD/CCYY  NAME        DESCRIPTION
 * 02/25/2026  jgilreath   Extracted from sqlWrite.cpp getTableDataFromDfile()
 * 02/27/2026  jgilreath   Re-implemented in SqlCommon namespace
 */
bool shouldIgnoreField(const QString& fieldName)
{
    /* Skip reserved fields (make sure to allow genuine ones) */
    if (fieldName.contains("reserved")) {
        /* Allow specific legitimate reserved fields */
        if (fieldName == "reserved_ssn_2"
            || fieldName == "for_reserve"
            || fieldName == "reserved_qty"
            || fieldName == "reserved_type") {
            return false;
        }
        return true;
    }

    /* Reserved by a different name */
    if (fieldName.contains("filler")) {
        return true;
    }

    /* Skip protect field (EZ-C specific) */
    if (fieldName.contains("protect")) {
        return true;
    }

    return false;
}

} // namespace SqlCommon
