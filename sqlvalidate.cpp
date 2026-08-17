/* sql_validate.cpp
 * Copyright 2026 AutoZone Inc.
 * Content is confidential to and proprietary information of AutoZone, Inc., its
 * subsidiaries and affiliates.
 *
 * EZ-C to PostgreSQL Validation Module
 *
 * This module provides validation functionality to compare EZ-C data
 * file records with their PostgreSQL equivalents. It uses the EZCMD
 * registry to reconstruct the EZ-C buffer from SQL data and performs
 * byte-level comparison.
 *
 * Primary API:
 *   dvalidate(DFILE *df) - Validates current record against PostgreSQL
 *
 * Debug Logging Control:
 *   By default, only ERROR and WARNING messages are logged to reduce
 *   log file size. Set these environment variables to enable verbose logging:
 *
 *   EZC_DEBUG_CACHE   - Enable cache loading progress messages
 *   EZC_DEBUG_RECORDS - Enable per-record match/mismatch status
 *   EZC_DEBUG_FIELDS  - Enable field-level validation details (most verbose)
 *
 *   Example:
 *     export EZC_DEBUG_CACHE=1
 *     ezc_validator.x poc.df -f
 *
 * Author: Blein Cancino
 * Date: 12/09/2025
 *
 * Modification History:
 *   MM/DD/CCYY  NAME        DESCRIPTION
 *   12/09/2025  bcancino    Initial implementation of validation module
 *   02/17/2026  bcancino    Added tiered debug logging to reduce log output
 */

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <QSet>
#include <QHash>
#include <QString>
#include <QVector>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QVariant>
#include "sqlCommon.h"

extern "C"
{
#include <sys/resource.h>
#include "datafile.h"
#include "sql_validate.h"
#include "azezc.h"
#include "misc.h"
#include "trace.h"
#include "ezcMD.h"
}

// Convenience macros for each debug level
#define aztraceDebugCache(...) aztraceDebug(debugCacheEnabled, __VA_ARGS__)
#define aztraceDebugRecords(...) aztraceDebug(debugRecordsEnabled, __VA_ARGS__)
#define aztraceDebugFields(...) aztraceDebug(debugFieldsEnabled, __VA_ARGS__)


/*! \brief Structure to hold cached SQL record data
 *
 * Stores all field values for a single SQL record
 * Note: The 'fields' map contains BOTH dedicated columns AND parsed
 * JSONB fields
 */
struct cachedSqlRecord
{
  qint64 id = 0;
  int formatType = 0;
  // fieldName -> value (includes JSONB fields)
  // stored as QByteArray for performance
  QHash<QString, QByteArray> fields;
};

/*! \brief Global cache for SQL records
 *
 * Maps tableName -> id -> cachedSqlRecord
 */
static QHash<QString, QHash<qint64, cachedSqlRecord>> SqlCache;
static bool CacheInitialized = false;

// Constants for magic numbers
static const int DEFAULT_EZC_FIELD_COUNT = 20;
static const int PROGRESS_INTERVAL = 10000;
static const int ESCAPE_BUFFER_RESERVE = 3;
static const int MEMORY_CHECK_INTERVAL = 50000;  // Check memory every 50K records

// Cache memory limit to prevent OOM
static const size_t MAX_CACHE_SIZE_GB = 15;  // Maximum cache size in gigabytes
static const size_t MAX_CACHE_SIZE_BYTES = MAX_CACHE_SIZE_GB * 1024UL * 1024 * 1024;

/*! \brief Debug level control via environment variables
 *
 * EZC_DEBUG_FIELDS  - Enable field-level logging (most verbose)
 * EZC_DEBUG_RECORDS - Enable per-record match/mismatch logging
 * EZC_DEBUG_CACHE   - Enable cache loading progress logging
 */
static inline bool debugFieldsEnabled()
{
  static int enabled = -1;  // Cache the result
  if (enabled == -1)
    {
      enabled = (getenv("EZC_DEBUG_FIELDS") != nullptr) ? 1 : 0;
    }
  return enabled == 1;
}

static inline bool debugRecordsEnabled()
{
  static int enabled = -1;  // Cache the result
  if (enabled == -1)
    {
      enabled = (getenv("EZC_DEBUG_RECORDS") != nullptr) ? 1 : 0;
    }
  return enabled == 1;
}

static inline bool debugCacheEnabled()
{
  static int enabled = -1;  // Cache the result
  if (enabled == -1)
    {
      enabled = (getenv("EZC_DEBUG_CACHE") != nullptr) ? 1 : 0;
    }
  return enabled == 1;
}

/*! \brief Get current process memory usage in bytes
 *
 * Uses getrusage() to retrieve maximum resident set size.
 *
 * \return Current memory usage in bytes, or 0 on error
 */
static size_t getCurrentMemoryUsage()
{
  struct rusage usage;
  if(getrusage(RUSAGE_SELF, &usage) != 0)
    {
      return 0;
    }
  // ru_maxrss is in kilobytes on Linux
  return static_cast<size_t>(usage.ru_maxrss) * 1024;
}

/*! \brief Generic debug trace wrapper
 *
 * Eliminates repetitive if(debugXXXEnabled()) checks throughout the code.
 * Only logs if the provided check function returns true.
 *
 * \param checkEnabled - Function pointer to debug enabled check
 * \param fmt - Printf-style format string
 * \param ... - Variable arguments for format string
 */
static void aztraceDebug(bool (*checkEnabled)(), const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void aztraceDebug(bool (*checkEnabled)(), const char *fmt, ...)
{
  if(!checkEnabled())
    {
      return;
    }

  va_list args;
  va_start(args, fmt);

  char buffer[2048];
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  aztrace("%s", buffer);

  va_end(args);
}

/*! \brief Get table name from EZ-C filename
 *
 * Converts EZ-C filename (e.g., "kitem.df") to table name (e.g., "kitem")
 *
 * \param filename - EZ-C filename
 * \param tableName - Output buffer for table name (min 32 bytes)
 * \return 0 on success, -1 on error
 */
static int getTableNameFromFilename(const char *filename, char *tableName)
{
  if((!filename) || (!tableName))
    {
      return -1;
    }

  // Copy filename and remove .df extension
  strncpy(tableName, filename, 31);
  tableName[31] = '\0';

  // Find and remove extension
  char *dot = strrchr(tableName, '.');
  if(dot)
    {
      *dot = '\0';
    }

  return 0;
}

/*! \brief Get set of ignored field names
 *
 * Returns a static hash set of field names that should be skipped
 * during validation.
 * These include:
 * - PostgreSQL system columns (id, formatType, created_at, updated_at)
 * - EZ-C internal fields (reserved, filler, protect)
 *
 * \return Reference to static hash set of ignored field names
 */
static const QSet<QString>& getIgnoredFields()
{
  static const QSet<QString> ignoredFields = 
    {
      // PostgreSQL system columns (not in EZ-C datafiles)
      "id", "format_type", "created_at", "updated_at",
      // EZ-C internal fields
      "filler", "filler_1", "filler_3", "filler_4", "filler1", "filler2",
      "filler3", "filler4", "filler5", "filler7", "filler8",
      "kcomazo_reserved", "klabels_reserved1", "kprichg_reserved",
      "kprichg_reserved1", "kprichg_reserved2",
      "protect", "protect_field", "protect_fld",
      "reserve", "reserve_1", "reserve_2", "reserve_3", "reserve_7",
      "reserve_field", "reserve_field0", "reserve_field1",
      "reserve_field2", "reserve0", "reserve1", "reserve10", "reserve12",
      "reserve13", "reserve14", "reserve15", "reserve16", "reserve17",
      "reserve18", "reserve19", "reserve2", "reserve20", "reserve3",
      "reserve4", "reserve5", "reserve6", "reserve8", "reserve9",
      "reserved", "reserved_00", "reserved_01", "reserved_02",
      "reserved_03", "reserved_04", "reserved_05", "reserved_06",
      "reserved_1", "reserved_10", "reserved_11", "reserved_111",
      "reserved_112", "reserved_12", "reserved_13", "reserved_136",
      "reserved_137", "reserved_138", "reserved_14", "reserved_142",
      "reserved_15", "reserved_16", "reserved_17", "reserved_18",
      "reserved_19",
      "reserved_2", "reserved_22", "reserved_23", "reserved_24",
      "reserved_25", "reserved_25_6", "reserved_26", "reserved_27",
      "reserved_28", "reserved_29", "reserved_29_45", "reserved_3",
      "reserved_30", "reserved_31", "reserved_32",
      "reserved_33", "reserved_34", "reserved_35", "reserved_36",
      "reserved_37", "reserved_38", "reserved_39", "reserved_4",
      "reserved_40", "reserved_41", "reserved_42", "reserved_43",
      "reserved_5", "reserved_59", "reserved_6", "reserved_60",
      "reserved_61", "reserved_62", "reserved_63", "reserved_64",
      "reserved_65", "reserved_66", "reserved_67", "reserved_7",
      "reserved_70", "reserved_8", "reserved_9", "reserved_field",
      "reserved_field_0", "reserved0", "reserved00", "reserved01",
      "reserved02", "reserved03", "reserved04", "reserved1", "reserved10",
      "reserved2", "reserved3", "reserved4", "reserved5", "reserved6",
      "reserved7", "reserved8", "reserved9", "reserver1", "reserverd",
      "reseved0"
    };
  return ignoredFields;
}

/*! \brief Parse JSONB data into field map
 *
 * Parses a PostgreSQL JSONB string and extracts all field-value pairs as strings.
 * EZ-C records store all data as text, so all JSON values are converted to strings.
 * This is done ONCE during cache load instead of during validation.
 *
 * Uses Qt's native JSON parser (QJsonDocument) for RFC-compliant parsing.
 * Skips ignored fields (reserved, filler, protect) to reduce cache size.
 *
 * \param jsonbData - Raw JSONB string from PostgreSQL
 * \param fieldsOut - Output map to populate with field name -> value pairs
 * \param ignoredFields - Set of field names to skip (for memory optimization)
 * \return Number of fields parsed
 */
static int parseJsonbFields(
    const QString& jsonbData,
    QHash<QString, QByteArray>& fieldsOut,
    const QSet<QString>& ignoredFields)
{
  if(jsonbData.isEmpty())
    {
      return 0;
    }

  // Parse JSON document
  QJsonParseError error;
  QJsonDocument doc = QJsonDocument::fromJson(jsonbData.toUtf8(), &error);

  // Handle parse errors
  if(error.error != QJsonParseError::NoError)
    {
      aztrace("JSON parse error at offset %d: %s\n",
              error.offset, error.errorString().toLocal8Bit().constData());
      return 0;
    }

  if(!doc.isObject())
    {
      return 0;
    }

  // Pre-allocate for typical EZ-C record
  fieldsOut.reserve(DEFAULT_EZC_FIELD_COUNT);

  // Extract fields from JSON object
  QJsonObject obj = doc.object();
  int fieldCount = 0;

  for(QJsonObject::iterator it = obj.begin(); it != obj.end(); ++it)
    {
      QString key = it.key();
      QJsonValue value = it.value();

      // Skip ignored fields (reserved, filler, protect)
      if(ignoredFields.contains(key))
        {
          continue;
        }

      // All EZ-C fields are strings; extract value directly
      QString valueStr = value.toString();
      if(valueStr.isEmpty())
        {
          continue;  // Skip empty values
        }

      // Store as QByteArray
      fieldsOut.insert(key.toLatin1(), valueStr.toLatin1());
      fieldCount++;
    }

  return fieldCount;
}

/*! \brief Load all records for a table into cache
 *
 * Loads all records from PostgreSQL for the given table into memory cache.
 * This is done once at validation start to avoid per-record queries.
 *
 * \param tableName - Table name to load
 * \return 0 on success, -1 on error
 */
static int loadTableCache(const char *tableName)
{
  if(!tableName)
    {
      aztrace("ERROR: loadTableCache() - nullptr table name\n");
      return -1;
    }

  // Check if already cached
  if(SqlCache.contains(tableName))
    {
      aztraceDebugCache("INFO: Table %s already cached (%d records)\n",
                   tableName, SqlCache[tableName].size());
      return 0;
    }

  aztraceDebugCache("INFO: Loading cache for table %s...\n", tableName);

  // Initialize and get database connection from SqlCommon
  // This uses credentials from azezc.ini with environment variable overrides
  using namespace SqlCommon;
  if(!initDatabase())
    {
      aztrace("ERROR: loadTableCache() - Failed to initialize database connection\n");
      return -1;
    }

  QSqlDatabase db = getDatabase();
  if(!db.isOpen())
    {
      aztrace("ERROR: loadTableCache() - Failed to connect to database: %s\n",
              db.lastError().text().toUtf8().constData());
      return -1;
    }

  // Check if table has any rows - skip if empty or doesn't exist
  char countQuery[512];
  snprintf(countQuery, sizeof(countQuery),
           "SELECT COUNT(*) FROM %s", tableName);

  aztraceDebugCache("INFO: Checking table size: %s\n", countQuery);

  QSqlQuery countResult(db);
  if(!countResult.exec(countQuery))
    {
      aztrace("ERROR: Count query failed for table %s: %s\n",
              tableName, countResult.lastError().text().toUtf8().constData());
      db.close();
      return -1;  // Failure - table should exist but doesn't
    }

  int rowCount = 0;
  if(countResult.next())
    {
      rowCount = countResult.value(0).toInt();
    }

  if(rowCount == 0)
    {
      aztraceDebugCache("INFO: Table %s is empty, skipping cache load\n", tableName);
      db.close();
      return 0;   // Return success - empty cache is valid
    }

  aztraceDebugCache("INFO: Table %s has %d rows, loading cache...\n",
               tableName, rowCount);

  // Query all records from the table
  char query[512];
  snprintf(query, sizeof(query), "SELECT * FROM %s", tableName);

  aztraceDebugCache("INFO: Executing: %s\n", query);

  QSqlQuery result(db);
  if(!result.exec(query))
    {
      aztrace("ERROR: loadTableCache() - Query failed: %s\n",
              result.lastError().text().toUtf8().constData());
      db.close();
      return -1;
    }

  aztraceDebugCache("INFO: Query returned %d rows\n", rowCount);

  // Pre-allocate cache capacity to avoid rehashing during inserts
  QHash<qint64, cachedSqlRecord>& tableCache = SqlCache[tableName];
  tableCache.reserve(rowCount);

  // Get column names from QSqlRecord
  QSqlRecord record = result.record();
  int columnCount = record.count();

  aztraceDebugCache("INFO: Discovered %d columns from query result\n", columnCount);

  // Build column name list and cache field indices
  QVector<QString> columnNames;
  QVector<int> columnIndices;

  int idFieldIdx = -1;
  int formatFieldIdx = -1;
  int attributesFieldIdx = -1;

  columnNames.reserve(columnCount);
  columnIndices.reserve(columnCount);

  for(int i = 0; i < columnCount; i++)
    {
      QString colName = record.fieldName(i);

      columnNames.append(colName);
      columnIndices.append(i);

      // Cache special field indices
      if(colName == "id")
        {
          idFieldIdx = i;
        }
      else if(colName == "format_type")
        {
          formatFieldIdx = i;
        }
      else if(colName == "attributes")
        {
          attributesFieldIdx = i;
        }
    }

  aztraceDebugCache("INFO: Found %d columns "
               "(idIdx=%d, format_idx=%d, attributes_idx=%d)\n",
               columnNames.size(), idFieldIdx, formatFieldIdx,
               attributesFieldIdx);

  // Get ignored fields set to skip during caching
  const QSet<QString>& ignoredFields = getIgnoredFields();

  // Process all rows using cached field indices
  int row = 0;
  while(result.next())
    {
      cachedSqlRecord cachedRec;

      // Get id and formatType using cached indices
      if(result.value(idFieldIdx).isNull() || result.value(formatFieldIdx).isNull())
        {
          aztrace("WARNING: Row %d missing id or format_type, skipping\n", row);
          row++;
          continue;
        }

      cachedRec.id = result.value(idFieldIdx).toLongLong();
      cachedRec.formatType = result.value(formatFieldIdx).toInt();

      // Use cached column indices
      for(int i = 0; i < columnNames.size(); i++)
        {
          const QString& colName = columnNames[i];
          int colIdx = columnIndices[i];

          // Skip 'attributes' column - it's the JSONB field
          // handled separately
          if(colIdx == attributesFieldIdx)
            {
              continue;
            }

          // Skip ignored fields (reserved, filler, protect)
          if(ignoredFields.contains(colName))
            {
              continue;
            }

          if(!result.value(colIdx).isNull())
            {
              cachedRec.fields[colName] = result.value(colIdx).toByteArray();
            }
        }

      // Parse JSONB attributes during cache load
      if(attributesFieldIdx >= 0 && !result.value(attributesFieldIdx).isNull())
        {
          QString jsonbStr = result.value(attributesFieldIdx).toString();
          if(!jsonbStr.isEmpty())
            {
              int jsonbFieldCount = parseJsonbFields(
                  jsonbStr, cachedRec.fields, ignoredFields);

              if((row + 1) % PROGRESS_INTERVAL == 0)
                {
                  aztraceDebugCache("  Parsed %d JSONB fields from row %d\n",
                               jsonbFieldCount, row + 1);
                }
            }
        }

      // Add to cache using id as key
      tableCache[cachedRec.id] = cachedRec;

      // Check memory usage periodically to prevent OOM
      if((row + 1) % MEMORY_CHECK_INTERVAL == 0)
        {
          size_t currentMemory = getCurrentMemoryUsage();
          if(currentMemory > MAX_CACHE_SIZE_BYTES)
            {
              double currentGB = currentMemory / (1024.0 * 1024.0 * 1024.0);

              fprintf(stdout, "ERROR: Cache exceeded %zuGB limit at %d/%d records (%.2fGB used)\n",
                      MAX_CACHE_SIZE_GB, row + 1, rowCount, currentGB);
              fflush(stdout);

              aztrace("ERROR: Cache exceeded %zuGB limit at %d/%d records (%.2fGB used)\n",
                      MAX_CACHE_SIZE_GB, row + 1, rowCount, currentGB);

              db.close();
              return -1;
            }

          aztraceDebugCache("INFO: Memory check at %d records: %.2f GB\n",
                       row + 1, currentMemory / (1024.0 * 1024.0 * 1024.0));
        }

      if((row + 1) % PROGRESS_INTERVAL == 0)
        {
          aztraceDebugCache("INFO: Cached %d records...\n", row + 1);
        }

      row++;
    }

  aztraceDebugCache("INFO: Successfully cached %d records for table %s "
               "(ignored %d field types for memory optimization)\n",
               static_cast<int>(tableCache.size()), tableName,
               ignoredFields.size());

  CacheInitialized = true;
  return 0;
}

/*! \brief Clear all cached data
 *
 * Clears the entire SQL record cache to free memory
 */
static void clearCache()
{
  aztraceDebugCache("INFO: Clearing SQL cache (%d tables cached)\n", SqlCache.size());
  SqlCache.clear();
  CacheInitialized = false;
}

/*! \brief Lookup record from cache
 *
 * \param tableName - Table name
 * \param id - Record ID
 * \param formatType - Format type
 * \return Pointer to cached record, or nullptr if not found
 */
static const cachedSqlRecord* lookupCache(
    const char *tableName, qint64 id)
{
  if(!SqlCache.contains(tableName))
    {
      return nullptr;
    }

  QHash<qint64, cachedSqlRecord>& tableCache = SqlCache[tableName];
  if(!tableCache.contains(id))
    {
      return nullptr;
    }

  return &tableCache[id];
}

/*! \brief Apply header filename exceptions for EZCMD schema lookup
 *
 * Maps datafile base names to their corresponding EZCMD schema names.
 * This handles cases where multiple datafiles share a single EZCMD schema
 * definition (e.g., dmdhist0-6 all use "dmdhist" schema).
 *
 * NOTE: This is ONLY used for EZCMD lookup, NOT for SQL table names.
 * SQL tables now use 1:1 mapping (dmdhist0.df → dmdhist0 table).
 *
 * \param baseName - Base datafile name without extension
 *                    (e.g., "dmdhist0", "kcomcuscont")
 * \param headerName - Output buffer for EZCMD schema name (min 32 bytes)
 * \return 0 on success, -1 on error
 */
static int applyHeaderExceptions(const char *baseName, char *headerName)
{
  if((!baseName) || (!headerName))
    {
      return -1;
    }

  // Define EZCMD schema name exceptions
  struct
    {
      const char *datafile;
      const char *schema;
    }
    exceptions[] =
      {
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
        { nullptr, nullptr }  // Sentinel
      };

  // Check if this baseName has an exception
  for(int i = 0; exceptions[i].datafile != nullptr; i++)
    {
      if(strcmp(baseName, exceptions[i].datafile) == 0)
        {
          strncpy(headerName, exceptions[i].schema, 31);
          headerName[31] = '\0';
          return 0;
        }
    }

  // No exception found, use baseName as-is
  strncpy(headerName, baseName, 31);
  headerName[31] = '\0';
  return 0;
}

/*! \brief Reconstruct EZ-C buffer from cached SQL data
 *
 * Rebuilds an EZ-C record buffer from pre-parsed PostgreSQL data
 * using EZCMD metadata.
 * Handles both text and numeric fields with proper alignment and padding.
 *
 * \param ezcmd - EZCMD metadata describing field layout
 * \param cachedRec - Pre-parsed cached record from PostgreSQL
 * \param sqlBuffer - Output buffer to populate (must be pre-allocated)
 * \param bufferSize - Size of output buffer
 *
 */
static void reconstructEzcBuffer(
    EZCMD *ezcmd,
    const cachedSqlRecord *cachedRec,
    char *sqlBuffer,
    int bufferSize)
{
  aztraceDebugFields("Reconstructing EZ-C buffer (%d bytes) "
                     "from pre-parsed cached data\n", bufferSize);

  // Reconstruct EZ-C buffer from pre-parsed cached SQL data using EZCMD
  for(int i = 0; i < ezcmd->count; i++)
    {
      const char *fieldName = ezcmd->fields[i];
      int offset = ezcmd->offsets[i];
      int length = ezcmd->lengths[i];
      const char *sqlValue = nullptr;

      // Lookup field value from pre-parsed cache
      // (dedicated column or JSONB field)
      QHash<QString, QByteArray>::const_iterator fieldIt =
          cachedRec->fields.constFind(fieldName);
      if(fieldIt != cachedRec->fields.constEnd())
        {
          sqlValue = fieldIt.value().constData();
          aztraceDebugFields("  Field '%s': value='%s'\n", fieldName, sqlValue);
        }

      if((sqlValue) && (strlen(sqlValue) > 0))
        {
          // Copy to buffer at EZ-C offset, respecting field length
          int copyLen = strlen(sqlValue);
          if(copyLen > length)
            {
              copyLen = length;  // Truncate if too long
            }

          // Copy value and left-align with space-padding on the right
          memcpy(sqlBuffer + offset, sqlValue, copyLen);
          if(copyLen < length)
            {
              memset(sqlBuffer + offset + copyLen, ' ',
                     length - copyLen);
            }
        }
      else
        {
          // Field has no value (NULL in SQL or skipped during cache load)
          // Fill entire field with spaces to match EZ-C format expectations
          // This ensures skipped fields (reserved_*, protect, filler) are properly space-filled
          memset(sqlBuffer + offset, ' ', length);
        }
    }
}

/*! \brief Allocate buffer for SQL record reconstruction
 *
 * Uses a static thread-local buffer for typical records (<16KB) to
 * avoid repeated allocations.
 * Falls back to dynamic allocation for larger records.
 *
 * \param size - Required buffer size
 * \param needsFree - [OUT] Set to true if buffer must be freed by caller
 *
 * \return Pointer to allocated buffer, or nullptr on allocation failure
 */
static char* allocateValidationBuffer(int size, bool* needsFree)
{
  // Use static buffer to avoid repeated allocations
  // Maximum EZ-C record size is typically <10KB
  static const int MAX_RECORD_SIZE = 16384;  // 16KB buffer
  static char sqlBuffer[MAX_RECORD_SIZE];

  if(size <= MAX_RECORD_SIZE)
    {
      // Use static buffer (common case - no allocation needed)
      *needsFree = false;
      memset(sqlBuffer, 0, size);
      return sqlBuffer;
    }
  else
    {
      // Fallback to dynamic allocation for large records
      char* buffer = static_cast<char*>(calloc(1, size));
      *needsFree = (buffer != nullptr);
      return buffer;
    }
}

/*! \brief Record field mismatch details for debugging and analysis
 *
 * Extracts and stores mismatch information for a single field including
 * field name, EZ-C value, SQL value, and metadata.
 * Handles control character escaping for readable output.
 *
 * \param mismatchInfo - [OUT] Structure to populate with mismatch details
 * \param fieldName - Name of the mismatched field
 * \param ezcBuffer - EZ-C record buffer
 * \param sqlBuffer - SQL record buffer
 * \param offset - Field offset in buffers
 * \param length - Field length
 */
static void recordMismatchDetails(
    MISMATCH_INFO *mismatchInfo,
    const char *fieldName,
    const char *ezcBuffer,
    const char *sqlBuffer,
    int offset,
    int length)
{
  if((!mismatchInfo) || (mismatchInfo->field_count >= MAX_MISMATCH_FIELDS))
    {
      return;
    }

  int idx = mismatchInfo->field_count;

  // Store field name
  strncpy(mismatchInfo->field_names[idx], fieldName,
          MAX_FIELD_NAME_LEN - 1);
  mismatchInfo->field_names[idx][MAX_FIELD_NAME_LEN - 1] = '\0';

  // Extract and store both EZC and SQL values
  // Convert null bytes to '\0' notation and show spaces as-is
  int outPosEzc = 0;
  int outPosSql = 0;
  for(int i = 0; i < length; i++)
    {
      // Process EZC buffer
      if(outPosEzc < MAX_FIELD_VALUE_LEN - ESCAPE_BUFFER_RESERVE)
        {
          unsigned char ch = ezcBuffer[offset + i];
          if(ch == '\0')
            {
              // Show null bytes as \0
              mismatchInfo->ezc_values[idx][outPosEzc++] = '\\';
              mismatchInfo->ezc_values[idx][outPosEzc++] = '0';
            }
          else if(ch < 32)
            {
              // Show control characters in hex
              outPosEzc += snprintf(
                  mismatchInfo->ezc_values[idx] + outPosEzc,
                  MAX_FIELD_VALUE_LEN - outPosEzc,
                  "\\x%02X", ch);
            }
          else
            {
              // Regular characters including spaces
              mismatchInfo->ezc_values[idx][outPosEzc++] = ch;
            }
        }

      // Process SQL buffer
      if(outPosSql < MAX_FIELD_VALUE_LEN - ESCAPE_BUFFER_RESERVE)
        {
          unsigned char ch = sqlBuffer[offset + i];
          if(ch == '\0')
            {
              // Show null bytes as \0
              mismatchInfo->sql_values[idx][outPosSql++] = '\\';
              mismatchInfo->sql_values[idx][outPosSql++] = '0';
            }
          else if(ch < 32)
            {
              // Show control characters in hex
              outPosSql += snprintf(
                  mismatchInfo->sql_values[idx] + outPosSql,
                  MAX_FIELD_VALUE_LEN - outPosSql,
                  "\\x%02X", ch);
            }
          else
            {
              // Regular characters including spaces
              mismatchInfo->sql_values[idx][outPosSql++] = ch;
            }
        }
    }
  mismatchInfo->ezc_values[idx][outPosEzc] = '\0';
  mismatchInfo->sql_values[idx][outPosSql] = '\0';

  // Store field metadata for root cause analysis
  mismatchInfo->field_lengths[idx] = length;
  mismatchInfo->field_offsets[idx] = offset;

  mismatchInfo->field_count++;

  aztraceDebugFields("  Field mismatch: %s - EZC='%s' SQL='%s' (len=%d)\n",
                     fieldName, mismatchInfo->ezc_values[idx],
                     mismatchInfo->sql_values[idx],
                     length);
}

/*! \brief Compare EZ-C and SQL buffers field-by-field and track mismatches
 *
 * Performs detailed field-by-field comparison between EZ-C and
 * reconstructed SQL buffers.
 * Skips ignored fields (reserved_*, protect, filler) and populates
 * mismatch details for non-ignored fields that differ.
 *
 * \param ezcmd - EZCMD metadata describing field layout
 * \param ezcBuffer - EZ-C record buffer
 *                     (from df->crec->databuffer)
 * \param sqlBuffer - Reconstructed SQL buffer
 * \param bufferSize - Size of buffers
 * \param mismatchInfo - [OUT] Structure to populate with mismatch
 *                        details (can be nullptr)
 *
 * \return true if any non-ignored fields have mismatches, false if all
 *         non-ignored fields match
 */
static bool compareBuffersFieldByField(
    EZCMD *ezcmd,
    const char *ezcBuffer,
    const char *sqlBuffer,
    int bufferSize,
    MISMATCH_INFO *mismatchInfo)
{
  // Initialize mismatchInfo if provided
  if(mismatchInfo)
    {
      memset(mismatchInfo, 0, sizeof(MISMATCH_INFO));
      mismatchInfo->field_count = 0;
    }

  bool hasMismatch = false;
  int totalMismatchCount = 0;

  // Get static set of ignored field names
  const QSet<QString>& ignoredFields = getIgnoredFields();

  // Track which fields have mismatches
  for(int fieldIdx = 0; fieldIdx < ezcmd->count; fieldIdx++)
    {
      int offset = ezcmd->offsets[fieldIdx];
      int length = ezcmd->lengths[fieldIdx];
      const char *fieldName = ezcmd->fields[fieldIdx];

      // Check if this is an ignored field
      // - skip comparison entirely if so
      bool isIgnored = ignoredFields.contains(fieldName);

      if(isIgnored)
        {
          // Skip ignored fields (reserved_*, protect, filler)
          // - no comparison needed
          continue;
        }

      // Calculate safe comparison length to avoid buffer overrun
      int cmpLength = (offset + length > bufferSize) ?
          (bufferSize - offset) : length;

      // If mismatch, find first byte offset for detailed logging
      if(memcmp(ezcBuffer+offset, sqlBuffer+offset, cmpLength) != 0)
        {
          for(int i = 0; i < cmpLength; i++)
            {
              int byteOffset = offset + i;
              if(ezcBuffer[byteOffset] != sqlBuffer[byteOffset])
                {
                  totalMismatchCount++;
                }
            }
          hasMismatch = true;

          // Record details if caller wants them
          recordMismatchDetails(mismatchInfo, fieldName, ezcBuffer, sqlBuffer,
                                offset, length);
        }
    }

  return hasMismatch;
}

/*! \brief Validate current DFILE record against PostgreSQL
 *
 * This is the core validation function (i.e. this is where it does
 * the "thing").
 * It:
 * 1. Gets EZCMD metadata for the current record format
 * 2. Queries PostgreSQL for the corresponding record
 * 3. Reconstructs an EZ-C buffer from the SQL data
 * 4. Compares the buffers byte-by-byte
 *    (skipping protected/reserved fields)
 * 5. Optionally tracks field-level mismatches
 *
 * \param df - Open DFILE pointer positioned at record to validate
 * \param mismatchInfo - Optional output parameter for detailed
 *                        mismatch info (can be nullptr)
 * \return 0 if match, 1 if mismatch, -1 if validation error
 *
 * Return values:
 *   0  = Match - all business fields match between EZ-C and
 *        PostgreSQL
 *   1  = Data mismatch - record exists but fields differ, or record
 *        missing from SQL
 *   -1 = Infrastructure error - cannot perform validation
 *        (missing metadata, allocation failure, etc.)
 *
 * Note: This function does NOT require SQL-backed DFILE. It validates
 *       traditional EZ-C files against PostgreSQL data.
 */
static int sqlValidate(DFILE *df, MISMATCH_INFO *mismatchInfo)
{
  if((!df) || (!df->crec))
    {
      aztrace("ERROR: sqlValidate() - Invalid DFILE pointer\n");
      return -1;  // Infrastructure error
    }

  char tableName[32];
  if(getTableNameFromFilename(df->filename, tableName) != 0)
    {
      aztrace("ERROR: sqlValidate() - Failed to parse table name "
              "from %s\n", df->filename);
      return -1;  // Infrastructure error
    }

  int formatType = df->crec->recfmt;

  // Adjust header file name if different from datafile/table
  char headerName[32];
  if(applyHeaderExceptions(tableName, headerName) != 0)
    {
      aztrace("ERROR: sqlValidate() - Failed to apply header "
              "exceptions for %s\n", tableName);
      return -1;  // Infrastructure error
    }

  aztraceDebugRecords("sqlValidate() - Validating datafile %s "
                      "(SQL table: %s, EZCMD schema: %s) format %d at seekp=%ld\n",
                      tableName, tableName, headerName, formatType, df->seekp);

  // Load table cache if not already loaded (use tableName for 1:1 SQL mapping)
  if(loadTableCache(tableName) != 0)
    {
      aztrace("ERROR: sqlValidate() - Failed to load cache for "
              "table %s\n", tableName);
      return -1;  // Infrastructure error
    }

  // Get EZCMD structure using headerName for schema lookup
  EZCMD *ezcmd = ezcmd_lookup(headerName, formatType);
  if(!ezcmd)
    {
      aztrace("ERROR: sqlValidate() - No EZCMD found for %s "
              "format %d\n",
              headerName, formatType);
      return -1;  // Infrastructure error
    }

  aztraceDebugRecords("sqlValidate() - Found EZCMD: %d fields, table=%s, "
                      "format=%d\n",
                      ezcmd->count, ezcmd->tname, ezcmd->format);

  // Lookup record from cache using seekp as ID
  // (use tableName for 1:1 SQL mapping)
  const cachedSqlRecord *cachedRec = lookupCache(
      tableName, static_cast<qint64>(df->seekp));
  if(!cachedRec)
    {
      aztrace("WARNING: sqlValidate() - No SQL record found in cache "
              "for id=%ld format=%d\n",
              df->seekp, formatType);

      // Populate mismatchInfo to indicate record not found in PostgreSQL
      if(mismatchInfo)
        {
          memset(mismatchInfo, 0, sizeof(MISMATCH_INFO));
          mismatchInfo->record_not_found = 1;
        }

      return 1;  // Data mismatch - record exists in EZ-C but not in PostgreSQL
    }

  aztraceDebugRecords("sqlValidate() - Found record in cache (id=%lld, format=%d)\n",
                      cachedRec->id, cachedRec->formatType);

  // Allocate buffer for SQL record reconstruction
  int bufferSize = df->cur_recsize;
  bool needsFree = false;
  char *sqlBuffer = allocateValidationBuffer(bufferSize, &needsFree);
  if(!sqlBuffer)
    {
      aztrace("ERROR: sqlValidate() - Failed to allocate buffer "
              "for large record (%d bytes)\n", bufferSize);
      return -1;  // Infrastructure error - memory allocation failure
    }

  // Reconstruct EZ-C buffer from cached SQL data
  reconstructEzcBuffer(ezcmd, cachedRec, sqlBuffer, bufferSize);

  // Compare buffers field-by-field and track mismatches
  bool hasMismatch = compareBuffersFieldByField(
      ezcmd,
      df->crec->databuffer,
      sqlBuffer,
      bufferSize,
      mismatchInfo);

  // Determine final result based on whether non-ignored fields matched
  // Note: Ignored fields (reserved_*, protect, filler) are skipped
  if(hasMismatch)
    {
      int fieldCount = mismatchInfo ? mismatchInfo->field_count : 0;
      aztraceDebugRecords("MISMATCH: %s format %d seekp=%ld - %d mismatches\n",
                          tableName, formatType, df->seekp, fieldCount);
    }
  else
    {
      // All business data fields match (ignored fields not compared)
      aztraceDebugRecords("MATCH: %s format %d seekp=%ld - all fields match\n",
                          tableName, formatType, df->seekp);
    }

  // Only free if we allocated dynamically
  if(needsFree)
    {
      free(sqlBuffer);
    }

  return hasMismatch ? 1 : 0;
}

// C wrapper functions for C++ implementation

/*! \brief Validate current EZ-C record against PostgreSQL
 *
 * Validates EZ-C record against PostgreSQL. If mismatchInfo is provided,
 * populates it with detailed field-level mismatch information.
 *
 * \param df - Data file pointer
 * \param mismatchInfo - Optional buffer for mismatch details (can be NULL)
 * \return 0 if match, 1 if mismatch, -1 if error
 */
extern "C" int dvalidate(DFILE *df, MISMATCH_INFO *mismatchInfo)
{
  if(!df)
    {
      return -1;  // Infrastructure error
    }

  // Perform validation
  return sqlValidate(df, mismatchInfo);
}

/*! \brief Initialize SQL cache for a datafile
 *
 * Loads all PostgreSQL records for the given datafile into memory cache.
 * Call this once before starting validation to enable batch loading.
 *
 * \param filename - Datafile name (e.g., "poc.df")
 * \return 0 on success, -1 on error
 */
extern "C" int azezc_sql_cache_init(const char *filename)
{
  if(!filename)
    {
      return -1;
    }

  char tableName[32];
  if(getTableNameFromFilename(filename, tableName) != 0)
    {
      return -1;
    }

  return loadTableCache(tableName);
}

/*! \brief Get SQL cache size for a datafile
 *
 * Returns the number of cached records for the given datafile.
 * Use this to check if a table has any data before validation.
 *
 * \param filename - Datafile name (e.g., "poc.df")
 * \return Number of cached records, or 0 if table is empty/not cached
 */
extern "C" int azezc_sql_cache_size(const char *filename)
{
  if(!filename)
    {
      return 0;
    }

  char tableName[32];
  if(getTableNameFromFilename(filename, tableName) != 0)
    {
      return 0;
    }

  // Check if table is cached
  if(!SqlCache.contains(tableName))
    {
      return 0;
    }

  return SqlCache[tableName].size();
}

/*! \brief Clear SQL cache
 *
 * Clears all cached SQL data to free memory.
 * Call this after validation is complete.
 */
extern "C" void azezc_sql_cache_clear()
{
  clearCache();
}

/*! \brief Get table name from filename
 *
 * Converts EZ-C filename (e.g., "kitem.df") to table name (e.g., "kitem")
 * C wrapper for internal getTableNameFromFilename function.
 *
 * \param filename - Datafile name
 * \param tableName - Output buffer for table name (min 32 bytes)
 * \return 0 on success, -1 on error
 */
extern "C" int azezc_get_table_name_from_filename(const char *filename,
                                                   char *table_name)
{
  return getTableNameFromFilename(filename, table_name);
}

/*! \brief Get EZCMD header name from datafile name
 *
 * Converts datafile name to EZCMD schema name, applying exceptions
 * for cases like dmdhist0-6 → dmdhist, invsusp1-3 → invsusp, etc.
 * C wrapper for internal applyHeaderExceptions function.
 *
 * \param datafile_name - Datafile base name (without .df extension)
 * \param header_name - Output buffer for EZCMD schema name (min 32 bytes)
 * \return 0 on success, -1 on error
 */
extern "C" int azezc_get_ezcmd_header_name(const char *datafile_name,
                                            char *header_name)
{
  return applyHeaderExceptions(datafile_name, header_name);
}

/*! \brief Get PostgreSQL table schema as JSON string
 *
 * Queries information_schema to get column names and types for the table.
 * Returns JSON array of column objects with name and type fields.
 *
 * \param table_name - Table name (e.g., "kitem", "poc")
 * \param schema_json - Output buffer for JSON string (caller must free)
 * \param buffer_size - Size of output buffer
 * \return 0 on success, -1 on error
 */
extern "C" int azezc_get_postgresql_table_schema(const char *table_name,
                                                  char *schema_json,
                                                  int buffer_size)
{
  if(!table_name || !schema_json || buffer_size <= 0)
    {
      return -1;
    }

  // Get database connection from SqlCommon
  // This uses credentials from azezc.ini with environment variable overrides
  using namespace SqlCommon;
  if(!initDatabase())
    {
      return -1;
    }

  QSqlDatabase db = getDatabase();
  if(!db.isOpen())
    {
      return -1;
    }

  // Query table schema from information_schema
  char schemaQuery[512];
  snprintf(schemaQuery, sizeof(schemaQuery),
           "SELECT column_name, data_type, character_maximum_length "
           "FROM information_schema.columns "
           "WHERE table_schema = 'ezc' AND table_name = '%s'"
           "ORDER BY ordinal_position", table_name);

  QSqlQuery result(db);
  if(!result.exec(schemaQuery))
    {
      db.close();
      return -1;
    }

  // Build JSON array with readable formatting
  int offset = 0;
  offset += snprintf(schema_json + offset, buffer_size - offset, "[\n");

  int row = 0;
  while(result.next())
    {
      QString colName = result.value(0).toString();
      QString data_type = result.value(1).toString();
      QString maxLength = result.value(2).toString();

      offset += snprintf(schema_json + offset, buffer_size - offset,
                         "    {\n      \"name\": \"%s\",\n"
                         "      \"type\": \"%s\"",
                         colName.isEmpty() ? "unknown" : colName.toUtf8().constData(),
                         data_type.isEmpty() ? "unknown" : data_type.toUtf8().constData());

      if(!maxLength.isEmpty())
        {
          offset += snprintf(schema_json + offset, buffer_size - offset,
                             ",\n      \"maxLength\": %s", maxLength.toUtf8().constData());
        }

      offset += snprintf(schema_json + offset, buffer_size - offset, "\n    }");

      // Check if there's a next row for comma placement
      bool hasNext = result.next();
      if(hasNext)
        {
          offset += snprintf(schema_json + offset, buffer_size - offset, ",");
          result.previous();  // Go back to process this row next time
        }

      offset += snprintf(schema_json + offset, buffer_size - offset, "\n");
      row++;
    }

  offset += snprintf(schema_json + offset, buffer_size - offset, "  ]");

  return 0;
}
