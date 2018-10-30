/*
 * main.c
 *
 *  Created on: 30.10.2017
 *      Author: jitschin
 */

/* required for printf */
#include <stdio.h>
/* required for close, access, R_OK */
#include <unistd.h>
/* required for open, SEEK_SET */
#include <fcntl.h>
/* required for strtok */
#include <string.h>
/* required for struct Vector */
#include "vector.h"
/* required for struct measurement_blob */
#include "measurement_blob.h"
/* required for datatype bool */
#include <stdbool.h>
/* required for datatype uint64_t */
#include <stdint.h>
/* required for atoi, strtoll, strtoull */
#include <stdlib.h>
/* required for strstr */
#include <string.h>
/* required for error numbers e.g. ECHILD */
#include <errno.h>
/* required for asynchronous threading, pthread_create, pthread_join, etc. */
#include <pthread.h>
/* required for struct varParams */
#include <stdbool.h>
/* required for metric plugin */
#include <scorep/SCOREP_MetricPlugins.h>

/** default buffer size for reading a file
 * MUST NOT BE SMALLER THAN 7 */
#define DEFAULT_BUFSIZE 4096
/** asumed default count of read logging data points */
#define BLOBARRAY_INIT_BUF 5000

/* TODO general:
 *
 * - define variables at first use ( yay C99 )
 * -
 */

/**
 * Stores the parameters of a given variable definition
 */
struct varParams
{
    int id;     /**< associated id for this spec (corresponds with logging id) */
    char* name; /**< associated name of this variable */
    bool doLog;           /**< whether this value was confirmed to be logged */
    struct blob_holder* logger;      /**< the associated logger instance */
    SCOREP_MetricValueType datatype; /**< datatype this variable is parsed to */
    int posRow;            /**< the row, i.e. line number where the sought field resides */
    int posCol;            /**< the column where the sought field resides */
    char posSep;           /**< the inter column separator */
    int logDif;            /**< if only a dif to the initial value shall be logged */
    int logPoint;          /**< if the data is to be shown as single points instead of a line */
    int inputHex;          /**< if the read data value is to be interpreted as hex value */
    int inputBinaryWidth;  /**< if the input is to be interpreted as binary */
    uint64_t binaryOffset; /**< the offset at which the binary value can be read */
    Fileparser_Binary_Datatype binaryDatatype; /**< the datatype of binary input data */
};
/**
 * Holds the filename and file descriptor of a set of variable definitions which are stored in
 * dataDefinitions
 */
struct fileParams
{
    char* filename;                   /**< where the file from which is read can be found */
    FILE* fileDescriptor;             /**< the latest file descriptor to the associated file */
    struct Vector* dataDefinitions;   /**< holds structs with varParams, with posRow, posCol, posSep
                                         parameters */
    struct Vector* binaryDefinitions; /**< holds structs with varParams, especially with
                                         inputBinaryWidth, binaryOffset, binaryDatatype */
    int isAccessible;                 /**< whether the file was accessible at initialization */
};

/**
 *  Simple struct to transport the read out values from file reading to logging
 */
struct foundValue
{
    struct varParams* associatedVarParams; /**< the corresponding varParams giving an insight about
                                              the values location */
    struct fileParams* associatedFileParams; /**< the corresponding fileParams hinting the
                                                associated filename */
    uint64_t associatedValue;                /**< the value to be stored/transported */
};

static struct varParams* getVarParamsForId(int32_t desiredId);
static int initializeLoggingFor(struct fileParams* fileSpec, struct varParams* varSpec);
static void processLine(struct fileParams* fileSpec, int* varParamsIndex, int curLineNumber,
                        struct Vector* foundValuesVec, char* myLine, bool verbose);
static void tryAppendingValueToFoundValuesVec(struct fileParams* fileSpec,
                                              struct varParams* varSpec,
                                              struct Vector* foundValuesVec, char* foundStr);
static int tryInsertingFileParams(struct fileParams* fileSpec);
static int tryInsertingVarParamsSorted(struct fileParams* fileSpec, struct varParams* varSpec);
static struct Vector* parseWholeFile(struct fileParams* fileSpec, bool verbose);
static FILE* prepareFileDescriptorForParsing(struct fileParams* fileSpec);
static struct fileParams* parseVariableSpecification(char* specStr, int idToBeAssigned);
static SCOREP_MetricValueType parseDatatype(char* curDatatypeName, int* inputHex, int* inputBinary,
                                            Fileparser_Binary_Datatype* binaryDatatype);
static int32_t init();
static void fini();
static void set_timer(uint64_t (*timer)(void));
static void* periodical_logging_thread(void* ignoredArgument);
SCOREP_Metric_Plugin_MetricProperties* get_event_info(char* event_name);
static int32_t add_counter(char* event_name);
static uint64_t get_all_values(int32_t id, SCOREP_MetricTimeValuePair** time_value_list);
SCOREP_METRIC_PLUGIN_ENTRY(fileparser_plugin);
static void log_error(char* errorMessage);
static void log_error_string(char* errorMessage, char* argumentToPrint);

static int count_of_counters = 0;
static int calls_to_event_info = 0;
static int successfull_logging_additions = 0;
static int calls_to_get_all_values = 0;
static struct Vector* fileParamsVector = NULL;
static volatile int logging_enabled;
static pthread_t logging_thread;
static uint64_t (*wtime)(void) = NULL;
static int sleep_duration = 100000;
static pthread_mutex_t logging_mutex = PTHREAD_MUTEX_INITIALIZER;
static char* readBuf = NULL;
static char* binaryBytesForParsing;
static struct Vector* unitStrPtrVec = NULL;

/**
 * Takes two pointers to define a substring in a char sequence which it copies into newly allocated
 * memory
 */
static char* allocSubstring(const char* strOffset, const char* strEnd)
{
    if (NULL == strOffset || NULL == strEnd || strOffset >= strEnd)
    {
        return NULL;
    }
    /* allocate new memory for the substring */
    int newStrLen = strEnd - strOffset;
    char* substring = malloc(newStrLen + 1);
    if (NULL == substring)
    {
        log_error("Could not allocate memory for a substring.");
        return NULL;
    }
    else
    {
        substring[newStrLen] = '\0';
        /* copy data into newly allocated memory */
        memcpy(substring, strOffset, newStrLen);

        return substring;
    }
}

/**
 * Initializer function invoked by Scorep
 */
static int32_t init()
{
    logging_enabled = 1;
    logging_thread = 0;

    /* check whether a logging interval is set */
    char* from_env = getenv("SCOREP_METRIC_FILEPARSER_PLUGIN_PERIOD");
    if (NULL != from_env)
    {
        sleep_duration = atoi(from_env);
        if (sleep_duration < 1)
        {
            sleep_duration = 100000;
        }
    }
    /* storage of the given variables */
    fileParamsVector = vec_create(4);
    if (NULL == fileParamsVector)
    {
        return 1;
    }
    /* general reading buffer to temporarily store read in bytes */
    readBuf = calloc(1, DEFAULT_BUFSIZE);
    if (NULL == readBuf)
    {
        log_error("Could not allocate readBuf for read in of the file.");
        return 1;
    }
    /* allocate buffer for parsing byte values */
    binaryBytesForParsing = calloc(8, sizeof(char));
    if (NULL == binaryBytesForParsing)
    {
        log_error("Could not allocate 8 bytes for binaryBytesForParsing.");
        return 1;
    }
    /* for some odd reason we have to keep track of the bytes passed as unit description
     * to delete them after the program is through */
    unitStrPtrVec = vec_create(5);
    if (NULL == unitStrPtrVec)
    {
        log_error("Could not allocate memory for a vector to store some backup pointers.");
    }

    return 0;
}
/**
 * Finalizer function invoked by Scorep
 */
static void fini()
{
    logging_enabled = 0;
    pthread_join(logging_thread, NULL);
    pthread_mutex_destroy(&logging_mutex);

    /*  cleanup, i.e. use destroy and free */
    if (NULL != fileParamsVector)
    {
        /* free the data of the fileParams and varParams specifications */
        for (int i = 0; i < fileParamsVector->length; ++i)
        {
            struct fileParams* fileSpec = fileParamsVector->data[i];
            for (int j = 0; j < fileSpec->dataDefinitions->length; ++j)
            {
                struct varParams* varSpec = fileSpec->dataDefinitions->data[j];
                free(varSpec->name);
                blobarray_destroy_subelements(varSpec->logger);
                free(varSpec->logger);
                free(varSpec);
            }
            vec_destroy(fileSpec->dataDefinitions);
            for (int j = 0; j < fileSpec->binaryDefinitions->length; ++j)
            {
                struct varParams* varSpec = fileSpec->binaryDefinitions->data[j];
                free(varSpec->name);
                blobarray_destroy_subelements(varSpec->logger);
                free(varSpec->logger);
                free(varSpec);
            }
            vec_destroy(fileSpec->binaryDefinitions);
            free(fileSpec->filename);
            if (NULL != fileSpec->fileDescriptor)
            {
                fclose(fileSpec->fileDescriptor);
            }
            free(fileSpec);
        }
        vec_destroy(fileParamsVector);
    }
    free(readBuf);
    free(binaryBytesForParsing);
    /* free the strings of the units */
    if (NULL != unitStrPtrVec)
    {
        for (int i = 0; i < unitStrPtrVec->length; ++i)
        {
            free(unitStrPtrVec->data[i]);
        }
        vec_destroy(unitStrPtrVec);
    }
}

/**
 * Simple function to get the timer function from Score-P
 */
static void set_timer(uint64_t (*timer)(void))
{
    wtime = timer;
}

/**
 * Thread function being started from add_counters, to run periodically during program execution
 */
static void* periodical_logging_thread(void* fileSpecVec)
{
    struct Vector* fileParamsVector = (struct Vector *) fileSpecVec;
    while (logging_enabled)
    {
        if (NULL == wtime)
        {
            continue;
        }
        pthread_mutex_lock(&logging_mutex);

        for (int i = 0; i < fileParamsVector->length; ++i)
        {
            struct Vector* foundValuesVec = parseWholeFile(fileParamsVector->data[i], false);
            if (NULL != foundValuesVec)
            {
                for (int j = 0; j < foundValuesVec->length; ++j)
                {
                    struct foundValue* curFound = foundValuesVec->data[j];
                    if(curFound->associatedVarParams->doLog && NULL != curFound->associatedVarParams->logger)
                    {
                        if (blobarray_append(curFound->associatedVarParams->logger,
                                             curFound->associatedValue, wtime(),
                                             curFound->associatedVarParams->logDif,
                                             curFound->associatedVarParams->datatype))
                        {
                            log_error("Ran out of memory when trying to memorize logging values.");
                        }
                    }
                    free(foundValuesVec->data[j]);
                }
                vec_destroy(foundValuesVec);
            }
        }
        pthread_mutex_unlock(&logging_mutex);
        usleep(sleep_duration);
    }
    return NULL;
}

/**
 * function called by Scorep to get some metadata on the measured fields
 */
SCOREP_Metric_Plugin_MetricProperties* get_event_info(char* event_name)
{
    ++calls_to_event_info;

    SCOREP_Metric_Plugin_MetricProperties* return_values =
        calloc(2, sizeof(SCOREP_Metric_Plugin_MetricProperties));
    if (NULL == return_values)
    {
        return NULL;
    }
    struct fileParams* fileSpec = parseVariableSpecification(event_name, count_of_counters);
    struct varParams* varSpec = NULL;
    int insertSuccessfull = 0;

    if (NULL != fileSpec && 0 < fileSpec->dataDefinitions->length)
    {
        varSpec = (struct varParams*)(fileSpec->dataDefinitions->data[0]);
        char* strDuplicate = strdup(varSpec->name);
        if (NULL != strDuplicate)
        {
            return_values[0].name = strDuplicate;
            strDuplicate = strdup("");
            if (NULL != strDuplicate)
            {
                return_values[0].unit = strDuplicate;
                return_values[0].base = SCOREP_METRIC_BASE_DECIMAL;
                return_values[0].exponent = 0;

                /* tell Score-P if it supposed to draw single data points or a line graph */
                if (1 == varSpec->logPoint)
                {
                    return_values[0].mode = SCOREP_METRIC_MODE_ABSOLUTE_POINT;
                }
                else
                {
                    return_values[0].mode = SCOREP_METRIC_MODE_ABSOLUTE_LAST;
                }
                /* tell Score-P what datatype it is receiving */
                return_values[0].value_type = varSpec->datatype;

                switch (tryInsertingFileParams(fileSpec))
                {
                case 2:
                    log_error(
                        "Could not insert variable specification to counters. Ran out of memory.");
                    break;
                case 1:
                    log_error(
                        "NULL Pointer error while inserting variable specification to counters.");
                    break;
                case 0:
                    insertSuccessfull = 1;
                    break;
                default:
                    log_error(
                        "Some error appeared while inserting variable specification to counters.");
                }
            }
        }
        if (insertSuccessfull)
        {
            /* keep track of unit strings, because SCORE-P does not free them of itself */
            vec_append(unitStrPtrVec, return_values[0].unit);
            ++count_of_counters;
        }
        else
        {
            if (NULL != varSpec)
            {
                free(varSpec->name);
                free(varSpec);
            }
            vec_destroy(fileSpec->dataDefinitions);
            vec_destroy(fileSpec->binaryDefinitions);
            free(fileSpec->filename);
            free(fileSpec);
            free(return_values[0].name);
            free(return_values[0].unit);
            return_values[0].name = NULL;
            return_values[0].unit = NULL;
        }
    }
    else
    {
        log_error_string("Could not parse variable specification \"%s\". Syntax incorrect?",
                         event_name);
    }

    return return_values;
}
/**
 * Tries to insert a fileParams's first varParams struct into the vector of the counters
 */
static int tryInsertingFileParams(struct fileParams* fileSpec)
{
    if (NULL == fileSpec)
    {
        return 1;
    }
    int wasInserted = 0;
    /* run through the vector of registered fileParams*/
    for (int i = 0; i < fileParamsVector->length; ++i)
    {
        struct fileParams* curFileParams = (struct fileParams*)fileParamsVector->data[i];
        int returnValue = 0;
        /* check if the current fileParams match the new fileParams */
        if (0 == strcmp(curFileParams->filename, fileSpec->filename))
        {
            /* try inserting the varSpec */
            returnValue =
                tryInsertingVarParamsSorted(curFileParams, fileSpec->dataDefinitions->data[0]);
            vec_destroy(fileSpec->dataDefinitions);
            vec_destroy(fileSpec->binaryDefinitions);
            free(fileSpec->filename);
            free(fileSpec);
            wasInserted = 1;
            return returnValue;
        }
    }
    if (!wasInserted)
    {
        int returnValue = vec_append(fileParamsVector, fileSpec);
        if (0 < ((struct varParams*)fileSpec->dataDefinitions->data[0])->inputBinaryWidth)
        {
            struct varParams* varSpec = fileSpec->dataDefinitions->data[0];
            fileSpec->dataDefinitions->data[0] = NULL;
            fileSpec->dataDefinitions->length = 0;
            if (vec_append(fileSpec->binaryDefinitions, varSpec))
            {
                vec_destroy(fileSpec->dataDefinitions);
                vec_destroy(fileSpec->binaryDefinitions);
                free(fileSpec->filename);
                free(fileSpec);
                return 3;
            }
        }
        return returnValue;
    }
    return 4; /* never reached, but written to make compiler happy */
}
/**
 * Tries to insert a varParams struct into a fileParams's vector in a sorted manner
 */
static int tryInsertingVarParamsSorted(struct fileParams* fileSpec, struct varParams* varSpec)
{
    if (NULL == fileSpec || NULL == varSpec)
    {
        return 1;
    }
    if (0 < varSpec->inputBinaryWidth)
    {
        if (!vec_append(fileSpec->binaryDefinitions, NULL))
        {
            int wasInserted = 0;
            /* run through all the elements in the vector looking for a varParams with a
             * binaryOffset set greater than the one to be inserted,
             * then inserts the new varParams before that */
            for (int i = 0; i < (fileSpec->binaryDefinitions->length - 1); ++i)
            {
                if (((struct varParams*)fileSpec->binaryDefinitions->data[i])->binaryOffset >
                    varSpec->binaryOffset)
                {
                    memmove(fileSpec->binaryDefinitions->data + i + 1,
                            fileSpec->binaryDefinitions->data + i,
                            sizeof(void*) * (fileSpec->binaryDefinitions->length - i - 1));
                    fileSpec->binaryDefinitions->data[i] = varSpec;
                    wasInserted = 1;
                    return 0;
                }
            }
            if (!wasInserted)
            {
                /* if no varParams was found greater than the new one, insert the new varParams at
                 * the end of the vector */
                fileSpec->binaryDefinitions->data[fileSpec->binaryDefinitions->length - 1] =
                    varSpec;
                return 0;
            }
        } else
        {
        	return 2;
        }
    }
    else
    {
        if (!vec_append(fileSpec->dataDefinitions, NULL))
        {
            int wasInserted = 0;
            /* run through all the elements in the vector looking for a varParams with a posRow set
             * greater than the one to be inserted, then inserts the new varParams before that */
            for (int i = 0; i < (fileSpec->dataDefinitions->length - 1); ++i)
            {
                if (((struct varParams*)fileSpec->dataDefinitions->data[i])->posRow >
                    varSpec->posRow)
                {
                    memmove(fileSpec->dataDefinitions->data + i + 1,
                            fileSpec->dataDefinitions->data + i,
                            sizeof(void*) * (fileSpec->dataDefinitions->length - i - 1));
                    fileSpec->dataDefinitions->data[i] = varSpec;
                    wasInserted = 1;
                    return 0;
                }
            }
            if (!wasInserted)
            {
                /* if no varParams was found greater than the new one, insert the new varParams at
                 * the end of the vector */
                fileSpec->dataDefinitions->data[fileSpec->dataDefinitions->length - 1] = varSpec;
                return 0;
            }
        }
        else
        {
            return 2;
        }
    }
    return 3; /* will never be reached. But written to make compiler happy */
}

/**
 * is called after get_event_info to add the counters (which I do in get_event_info) in here it
 * initializes the logging containers
 */
static int32_t add_counter(char* event_name)
{
    /* run through all the fileParams, scanning the files for an initialization value */
    //DONE: with each call of add_counter find the corresponding, registered metric, and start the logging for it
    int matchingId = -1;
    struct fileParams* matchingFileSpec = NULL;
    for (int i = 0; (i < fileParamsVector->length && NULL == matchingFileSpec); ++i)
    {
    	struct fileParams* curFileSpec = fileParamsVector->data[i];
    	for(int j = 0; (j < curFileSpec->dataDefinitions->length && NULL == matchingFileSpec); ++j) {
    		struct varParams* curVarSpec = (struct varParams*) curFileSpec->dataDefinitions->data[j];
    	    if(0 == strcmp(curVarSpec->name, event_name))
    	    {
    	    	if(!initializeLoggingFor(curFileSpec, curVarSpec))
    	    	{
        	        matchingFileSpec = curFileSpec;
        	        matchingId = curVarSpec->id;
    	    	}

    	    }
        }
    	for(int j = 0; (j < curFileSpec->binaryDefinitions->length && NULL == matchingFileSpec); ++j) {
    		struct varParams* curVarSpec = (struct varParams*) curFileSpec->binaryDefinitions->data[j];
    	    if(0 == strcmp(curVarSpec->name, event_name))
    	    {
    	    	if(!initializeLoggingFor(curFileSpec, curVarSpec))
    	    	{
        	        matchingFileSpec = curFileSpec;
        	        matchingId = curVarSpec->id;
    	    	}
    	    }
        }
    }

    if(NULL != matchingFileSpec)
    {
    	++successfull_logging_additions;
    	if(1 == successfull_logging_additions)
    	{
    	    /* start thread to read out the individual files periodically */
    	    if (pthread_create(&logging_thread, NULL, &periodical_logging_thread, fileParamsVector))
    	    {
    	        log_error("Can't start logging thread.\n");
    	        return -ECHILD;
    	    }
    	}
   } else
   {
        log_error_string("Could not match up add_counter(\"%s\") with previous get_event_info", event_name);
        return -1;
   }

    return matchingId; // this return value defines the id with which the counter will be associated
}
/**
 * Helper function to read the first values from a fileSpec and initialize logging on the specified varSpec
 */
static int initializeLoggingFor(struct fileParams* fileSpec, struct varParams* varSpec)
{
	if (!access(fileSpec->filename, R_OK))
	{
		fileSpec->isAccessible = 1; //TODO: heed this flag down below in function prepareFileDescriptor!
	}
	else
	{
		log_error_string("File \"%s\" can not be accessed for reading.",
				fileSpec->filename);
	}
	struct Vector* foundValuesVec = parseWholeFile(fileSpec, true);
    bool couldInitialize = false;

	if (NULL != foundValuesVec)
	{
		/* run through individual read outs  */
		for (int j = 0; j < foundValuesVec->length; ++j)
		{
			/* try to create a new blob_holder/logging container */
			struct foundValue* curFound = foundValuesVec->data[j];
			if (curFound->associatedVarParams == varSpec)
			{
				struct blob_holder* newLoggingHolder =
						blobarray_create(BLOBARRAY_INIT_BUF, curFound->associatedValue);
				if (NULL != newLoggingHolder)
				{
					curFound->associatedVarParams->logger = newLoggingHolder;
					couldInitialize = true;
				}
				else
				{
					log_error("Could not allocate a few bytes of memory to create a blob_holder.");
				}
			}
			free(foundValuesVec->data[j]);
		}
		vec_destroy(foundValuesVec);
	}

	if(couldInitialize)
	{
		varSpec->doLog = true;
		return 0;
	} else
	{
		return 1;
	}
}
/**
 * Required function for Scorep to read out the logged data
 */
static uint64_t get_all_values(int32_t id, SCOREP_MetricTimeValuePair** time_value_list)
{
    int saved_nr_results = 0;
    /* try allocating memory */
    pthread_mutex_lock(&logging_mutex);
    if (-1 < id && id < count_of_counters)
    {
    	struct varParams* varSpec = getVarParamsForId(id);
    	if(NULL != varSpec && varSpec->doLog) {
            saved_nr_results =
                blobarray_get_TimeValuePairs(varSpec->logger, time_value_list);
            if (0 > saved_nr_results)
            {
                log_error("Could not allocate memory for passing logging data to Score-P.\n");
                pthread_mutex_unlock(&logging_mutex);
                return 0;
            }
            blobarray_reset(varSpec->logger);
    	}
    }
    pthread_mutex_unlock(&logging_mutex);

    ++calls_to_get_all_values;

    return saved_nr_results;
}

/**
 * Iterates over FileParamsVector to find the corresponding VarParams Data
 */
static struct varParams* getVarParamsForId(int32_t desiredId)
{
    struct varParams* desiredVarSpec = NULL;
    for(int i = 0; (i < fileParamsVector->length && NULL == desiredVarSpec); ++i)
    {
    	struct fileParams* curFileSpec = (struct fileParams*) fileParamsVector->data[i];
    	for(int j = 0; (j < curFileSpec->dataDefinitions->length && NULL == desiredVarSpec); ++j)
    	{
    		if(((struct varParams*)curFileSpec->dataDefinitions->data[j])->id == desiredId)
    		{
    			desiredVarSpec = (struct varParams*) curFileSpec->dataDefinitions->data[j];
    		}
    	}
    	for(int j = 0; (j < curFileSpec->binaryDefinitions->length && NULL == desiredVarSpec); ++j)
    	{
    		if(((struct varParams*)curFileSpec->binaryDefinitions->data[j])->id == desiredId)
    		{
    			desiredVarSpec = (struct varParams*) curFileSpec->binaryDefinitions->data[j];
    		}
    	}
    }

    return desiredVarSpec;
}
/**
 * Parses a single variable argument returning a pointer to a nice struct
 *
 * Can parse strings like these:
 * netstat:int@/proc/net/netstat+c=7;r=3s=
 * dev    :int@/proc/net/dev+c=1;r=2;d
 *
 * Format:
 * name:datatype@/path/to/file+parameters
 * where datatype can be
 *  int:    int64_t
 *  uint:   uint64_t
 *  float:  double
 *  double: double
 * where parameters are
 *  c: column (number of char in the line)
 *  r: row (number of the line)
 *  s: separator (character in between data field)
 *  d: diff (only shows a diff from a starting value)
 *  p: point (show logged data as point values)
 */
static struct fileParams* parseVariableSpecification(char* specStr, int idToBeAssigned)
{
    if (NULL == specStr)
    {
        return NULL;
    }
    struct fileParams* parsedData;
    parsedData = calloc(1, sizeof(struct fileParams));
    if (NULL == parsedData)
    {
        log_error("Ran out of memory when trying to allocate a few bytes for a fileParams struct.");
        return NULL;
    }
    parsedData->dataDefinitions = vec_create(1);
    parsedData->binaryDefinitions = vec_create(1);

    if (NULL == parsedData->dataDefinitions || NULL == parsedData->binaryDefinitions)
    {
        log_error_string(
            "Could not allocate memory for vectors in struct fileParams. Input specStr was \"%s\".",
            specStr);
        free(parsedData);
        vec_destroy(parsedData->dataDefinitions);
        vec_destroy(parsedData->binaryDefinitions);
        return NULL;
    }

    char* curStrOffset = NULL;
    char* posOfCurDelim = NULL;

    /* Parse variable name */
    curStrOffset = specStr;
    posOfCurDelim = strchr(specStr, ':');
    char* curVarName = allocSubstring(specStr, posOfCurDelim);
    if (NULL != posOfCurDelim)
    {
        curStrOffset = posOfCurDelim + 1;
    }

    /* Parse Datatype argument */
    posOfCurDelim = strchr(curStrOffset, '@');
    char* curDatatypeName = allocSubstring(curStrOffset, posOfCurDelim);
    if (NULL != posOfCurDelim)
    {
        curStrOffset = posOfCurDelim + 1;
    }
    int inputHex = 0;
    int inputBinaryWidth = 0;
    Fileparser_Binary_Datatype binaryDatatype = FILEPARSER_BINARY_DATATYPE_UNDEFINED;
    SCOREP_MetricValueType curDatatype =
        parseDatatype(curDatatypeName, &inputHex, &inputBinaryWidth, &binaryDatatype);

    /* Parse Filename */
    posOfCurDelim = strchr(curStrOffset, '+');
    char* curFilename = allocSubstring(curStrOffset, posOfCurDelim);

    /* variables required for parsing the position argument */
    char* nextPosToken = NULL;
    char* curPosToken = NULL;
    char* posOfEqualsInPos = NULL;
    int posRow = 0, posCol = 0, logDif = 0, logPoint = 0;
    uint64_t binaryOffset = 0;
    char posSep = ' ';

    /* Parse Position parameters */
    if (NULL != posOfCurDelim)
    {
        /* Tokenize position parameters using delimiter semi-colon */
        curPosToken = strtok_r(posOfCurDelim + 1, ";", &nextPosToken);
        while (NULL != curPosToken)
        {

            /* parse individual positional tokens */
            posOfEqualsInPos = strchr(curPosToken, '=');
            switch (curPosToken[0])
            {
            case 'C': /* fall-through */
            case 'c':
                if (NULL != posOfEqualsInPos)
                {
                    posCol = atoi(posOfEqualsInPos + 1);
                }
                else
                {
                    log_error("Can't parse option C, no parameter provided.");
                }
                break;
            case 'L': /* fall-through */
            case 'l': /* fall-through */
            case 'R': /* fall-through */
            case 'r':
                if (NULL != posOfEqualsInPos)
                {
                    posRow = atoi(posOfEqualsInPos + 1);
                }
                else
                {
                    log_error("Can't parse option R, no parameter provided.");
                }
                break;
            case 'S': /* fall-through */
            case 's':
                if (NULL != posOfEqualsInPos)
                {
                    posSep = (posOfEqualsInPos + 1)[0];
                }
                else
                {
                    log_error("Can't parse option S, no parameter provided.");
                }
                break;
            case 'D': /* fall-through*/
            case 'd':
                logDif = 1;
                break;
            case 'P': /* fall-through*/
            case 'p':
                logPoint = 1;
                break;
            case 'A': /* fall-through */
            case 'a':
                logPoint = 0;
                break;
            case 'B': /* fall-through */
            case 'b':
                if (NULL != posOfEqualsInPos)
                {
                    char* endPtr;
                    binaryOffset = strtoull(posOfEqualsInPos + 1, &endPtr, 0);
                }
                else
                {
                    log_error("Can't parse option B, no parameter provided.");
                }
                break;
            }
            curPosToken = strtok_r(NULL, ";", &nextPosToken);
        }

        /* try setting up new varSpec */
        struct varParams* varSpec = calloc(1, sizeof(struct varParams));
        if (NULL == varSpec)
        {
            log_error(
                "Ran out of memory when trying to allocate a few bytes for a varParams struct.");
            vec_destroy(parsedData->dataDefinitions);
            vec_destroy(parsedData->binaryDefinitions);
            free(parsedData);
            free(curVarName);
            free(curDatatypeName);
            free(curFilename);
            return NULL;
        }

        /* assign the actual values */
        varSpec->id = idToBeAssigned;
        varSpec->name = curVarName;
        varSpec->doLog = false;
        varSpec->datatype = curDatatype;
        parsedData->filename = curFilename;
        parsedData->fileDescriptor = NULL;
        varSpec->posCol = posCol;
        varSpec->posRow = posRow;
        varSpec->posSep = posSep;
        varSpec->logDif = logDif;
        varSpec->logPoint = logPoint;
        varSpec->inputHex = inputHex;
        varSpec->inputBinaryWidth = inputBinaryWidth;
        varSpec->binaryOffset = binaryOffset;
        varSpec->binaryDatatype = binaryDatatype;

        /* oh no, we could not append this definition, return NULL */
        if (vec_append(parsedData->dataDefinitions, varSpec))
        {
            log_error("Ran out of memory when trying to insert a varParams struct into a vector of "
                      "a fileParams struct.");
            vec_destroy(parsedData->dataDefinitions);
            vec_destroy(parsedData->binaryDefinitions);
            free(parsedData);
            free(curVarName);
            free(curDatatypeName);
            free(curFilename);
            return NULL;
        }
    }
    else
    {
        /* invalid variable given
         * discard data (i.e. do a free on all the allocated strings) */
        vec_destroy(parsedData->dataDefinitions);
        vec_destroy(parsedData->binaryDefinitions);
        free(parsedData);
        free(curVarName);
        free(curDatatypeName);
        free(curFilename);
        return NULL;
    }
    free(curDatatypeName);

    return parsedData;
}

/**
 * Parses a given curDatatypeName returning the read datatype and setting the arguments accordingly
 */
static SCOREP_MetricValueType parseDatatype(char* curDatatypeName, int* inputHex,
                                            int* inputBinaryWidth,
                                            Fileparser_Binary_Datatype* binaryDatatype)
{
    SCOREP_MetricValueType curDatatype = SCOREP_METRIC_VALUE_INT64;

    if (NULL == curDatatypeName || 0 == strcasecmp(curDatatypeName, "int"))
    {
        curDatatype = SCOREP_METRIC_VALUE_INT64;
    }
    else if (0 == strcasecmp(curDatatypeName, "int_hex"))
    {
        curDatatype = SCOREP_METRIC_VALUE_INT64;
        *inputHex = 1;
    }
    else if (0 == strcasecmp(curDatatypeName, "uint"))
    {
        curDatatype = SCOREP_METRIC_VALUE_UINT64;
    }
    else if (0 == strcasecmp(curDatatypeName, "uint_hex"))
    {
        curDatatype = SCOREP_METRIC_VALUE_UINT64;
        *inputHex = 1;
    }
    else if (0 == strcasecmp(curDatatypeName, "float") ||
             0 == strcasecmp(curDatatypeName, "double"))
    {
        curDatatype = SCOREP_METRIC_VALUE_DOUBLE;
    }
    else if (0 == strcasecmp(curDatatypeName, "int8_bin"))
    {
        *binaryDatatype = FILEPARSER_BINARY_DATATYPE_INT8;
        *inputBinaryWidth = 1;
        curDatatype = SCOREP_METRIC_VALUE_INT64;
    }
    else if (0 == strcasecmp(curDatatypeName, "int16_bin"))
    {
        *binaryDatatype = FILEPARSER_BINARY_DATATYPE_INT16;
        *inputBinaryWidth = 2;
        curDatatype = SCOREP_METRIC_VALUE_INT64;
    }
    else if (0 == strcasecmp(curDatatypeName, "int32_bin"))
    {
        *binaryDatatype = FILEPARSER_BINARY_DATATYPE_INT32;
        *inputBinaryWidth = 4;
        curDatatype = SCOREP_METRIC_VALUE_INT64;
    }
    else if (0 == strcasecmp(curDatatypeName, "int64_bin"))
    {
        *binaryDatatype = FILEPARSER_BINARY_DATATYPE_INT64;
        *inputBinaryWidth = 8;
        curDatatype = SCOREP_METRIC_VALUE_INT64;
    }
    else if (0 == strcasecmp(curDatatypeName, "uint8_bin"))
    {
        *binaryDatatype = FILEPARSER_BINARY_DATATYPE_UINT8;
        *inputBinaryWidth = 1;
        curDatatype = SCOREP_METRIC_VALUE_UINT64;
    }
    else if (0 == strcasecmp(curDatatypeName, "uint16_bin"))
    {
        *binaryDatatype = FILEPARSER_BINARY_DATATYPE_UINT16;
        *inputBinaryWidth = 2;
        curDatatype = SCOREP_METRIC_VALUE_UINT64;
    }
    else if (0 == strcasecmp(curDatatypeName, "uint32_bin"))
    {
        *binaryDatatype = FILEPARSER_BINARY_DATATYPE_UINT32;
        *inputBinaryWidth = 4;
        curDatatype = SCOREP_METRIC_VALUE_UINT64;
    }
    else if (0 == strcasecmp(curDatatypeName, "uint64_bin"))
    {
        *binaryDatatype = FILEPARSER_BINARY_DATATYPE_UINT64;
        *inputBinaryWidth = 8;
        curDatatype = SCOREP_METRIC_VALUE_UINT64;
    }
    else if (0 == strcasecmp(curDatatypeName, "float_bin"))
    {
        *binaryDatatype = FILEPARSER_BINARY_DATATYPE_FLOAT;
        *inputBinaryWidth = 4;
        curDatatype = SCOREP_METRIC_VALUE_DOUBLE;
    }
    else if (0 == strcasecmp(curDatatypeName, "double_bin"))
    {
        *binaryDatatype = FILEPARSER_BINARY_DATATYPE_DOUBLE;
        *inputBinaryWidth = 8;
        curDatatype = SCOREP_METRIC_VALUE_DOUBLE;
    }
    else
    {
        log_error_string("Could not parse datatype \"%s\".", curDatatypeName);
    }

    return curDatatype;
}

/**
 * Parses a whole file according to parameters given with fileSpec and returns an array of found
 * results "Files öffnen und schliessen ist immer sehr teuer" (Robert Schöne) DONE: declare the used
 * variables as static, thereby they will be kept as global variables (within the file; even when I
 * declare them within a method to be static) of course this also means I will have to allocate them
 * at the beginning and have to free them at the end
 */
static struct Vector* parseWholeFile(struct fileParams* fileSpec, bool verbose)
{

    /* Try to get a file descriptor pointing to the beginning of the file */
    FILE* fileDescriptor = prepareFileDescriptorForParsing(fileSpec);
    if (NULL == fileDescriptor || 0 == fileSpec->isAccessible)
    {
        return NULL;
    }

    struct Vector* foundValuesVec = vec_create(fileSpec->dataDefinitions->length);
    if (NULL == foundValuesVec)
    {
        log_error("Could not allocate vector to store the values found in a file.");
        return NULL;
    }

    size_t readReturn = -1;
    char* overlapBuf = NULL;
    char* overlapSwapBuf = NULL;
    char* curNewlineIndex = NULL;
    char* prevIndex = NULL;
    int curLineNumber = 0;
    int varParamsIndex = 0;
    int binaryParamsIndex = 0;
    uint64_t curTotalBytesRead = 0;
    char* binaryOverlapBuf = NULL;

    if (0 == fileSpec->binaryDefinitions->length)
    {
        binaryParamsIndex = -1;
    }
    do
    {
        /* do read from file */
        readReturn = fread(readBuf, 1, DEFAULT_BUFSIZE - 1, fileDescriptor);
        if (0 < readReturn)
        {
            curTotalBytesRead += readReturn;
            readBuf[readReturn] = '\0';

            /* check if there are binary reads within this chunk of read bytes to be performed */
            if (-1 < binaryParamsIndex)
            {
                struct varParams* binaryVarSpec =
                    fileSpec->binaryDefinitions->data[binaryParamsIndex];
                while (NULL != binaryVarSpec && binaryVarSpec->binaryOffset < curTotalBytesRead)
                {
                    if (NULL != binaryOverlapBuf)
                    {
                        /* copy both the fragment of old bytes and freshly read bytes to
                         * binaryBytesForParsing */
                        memcpy(binaryBytesForParsing, binaryOverlapBuf,
                               curTotalBytesRead - readReturn - binaryVarSpec->binaryOffset);
                        int byteCountStillToCopy =
                            binaryVarSpec->inputBinaryWidth -
                            (curTotalBytesRead - readReturn - binaryVarSpec->binaryOffset);
                        memcpy(binaryBytesForParsing + byteCountStillToCopy, readBuf,
                               byteCountStillToCopy);

                        /* actually register the read bytes */
                        tryAppendingValueToFoundValuesVec(fileSpec, binaryVarSpec, foundValuesVec,
                                                          binaryBytesForParsing);
                        free(binaryOverlapBuf);
                    }
                    else
                    {
                        /* is binaryOffset within the current readBuf? */
                        if (binaryVarSpec->binaryOffset > (curTotalBytesRead - readReturn))
                        {
                            if ((binaryVarSpec->binaryOffset + binaryVarSpec->inputBinaryWidth) <=
                                curTotalBytesRead)
                            {
                                /* copy the found bytes for appending them to foundValues later */
                                memcpy(binaryBytesForParsing,
                                       readBuf + (binaryVarSpec->binaryOffset -
                                                  (curTotalBytesRead - readReturn)),
                                       binaryVarSpec->inputBinaryWidth);
                                tryAppendingValueToFoundValuesVec(
                                    fileSpec, binaryVarSpec, foundValuesVec, binaryBytesForParsing);
                            }
                            else
                            {
                                /* the current readBuf does not contain all bytes that we wish to
                                 * read, utilize binaryOverlapBuf */
                                binaryOverlapBuf = calloc(8, sizeof(char));
                                if (NULL != binaryOverlapBuf)
                                {
                                    memcpy(binaryOverlapBuf,
                                           readBuf + (binaryVarSpec->binaryOffset -
                                                      (curTotalBytesRead - readReturn)),
                                           curTotalBytesRead - binaryVarSpec->binaryOffset);
                                }
                                else
                                {
                                    log_error("Could not read binary value, allocation of 8 bytes "
                                              "failed.");
                                }
                                continue;
                            }
                        }
                        else
                        {
                            log_error("This error should never appear. binaryVarSpec->binaryOffset "
                                      "is smaller than (curTotalBytesRead - readReturn).");
                        }
                    }

                    /* incremental operation, the usual loop stuff */
                    ++binaryParamsIndex;
                    if (fileSpec->binaryDefinitions->length > binaryParamsIndex)
                    {
                        binaryVarSpec = fileSpec->binaryDefinitions->data[binaryParamsIndex];
                    }
                    else
                    {
                        binaryVarSpec = NULL;
                    }
                }
            }

            curNewlineIndex = strchr(readBuf, '\n');
            /* if no newline was found in the current read out, then append it all to overlapBuf */
            if (NULL == curNewlineIndex)
            {
                if (NULL == overlapBuf)
                {
                    overlapBuf = "";
                }
                overlapSwapBuf = malloc(strlen(overlapBuf) + readReturn + 1);
                if (NULL == overlapSwapBuf)
                {
                    log_error("Could not allocate memory for storing the read from readBuf.");
                    free(overlapBuf);
                    vec_destroy(foundValuesVec);
                    return NULL;
                }
                sprintf(overlapSwapBuf, "%s%s", overlapBuf, readBuf);
                overlapBuf = overlapSwapBuf;
            }
            else
            {
                /* we found a newline in the current read out, thus we can process it */
                prevIndex = readBuf;

                /* overlapBuf is not empty, i.e. last call to read did not end with a newline */
                if (NULL != overlapBuf)
                {
                    curNewlineIndex[0] = '\0';

                    /* try allocating some memory for the current line */
                    int tempLen = strlen(overlapBuf) + strlen(readBuf) + 1;
                    overlapSwapBuf = malloc(tempLen);
                    if (NULL == overlapSwapBuf)
                    {
                        log_error(
                            "Could not allocate memory for parsing the current line in a file.");
                        free(overlapBuf);
                        vec_destroy(foundValuesVec);
                        return NULL;
                    }
                    sprintf(overlapSwapBuf, "%s%s", overlapBuf, readBuf);
                    free(overlapBuf);
                    overlapBuf = overlapSwapBuf;

                    processLine(fileSpec, &varParamsIndex, curLineNumber, foundValuesVec,
                                overlapBuf, verbose);
                    ++curLineNumber;

                    /* a bit of cleanup */
                    free(overlapBuf);
                    overlapBuf = NULL;
                    prevIndex = curNewlineIndex + 1;
                    curNewlineIndex = strchr(curNewlineIndex + 1, '\n');
                }
                /* go through readBuf processing each line */
                for (; NULL != curNewlineIndex; curNewlineIndex = strchr(curNewlineIndex + 1, '\n'))
                {
                    curNewlineIndex[0] = '\0';
                    processLine(fileSpec, &varParamsIndex, curLineNumber, foundValuesVec,
                                prevIndex, verbose);
                    ++curLineNumber;

                    prevIndex = curNewlineIndex + 1;
                }
                /* process remainder/trailing line, put it in overlapBuf */
                if ((readBuf + readReturn) > prevIndex)
                {
                    overlapBuf = strdup(prevIndex);
                }
            }
        }

    } while (0 < readReturn);

    /* process last line */
    if (NULL != overlapBuf)
    {
        processLine(fileSpec, &varParamsIndex, curLineNumber, foundValuesVec, overlapBuf, verbose);
        ++curLineNumber;
        free(overlapBuf);
    }

    return foundValuesVec;
}
/**
 * Tries to get a file Descriptor for the file denoted in varParams, if a fileDescriptor is already
 * there, seek to beginning of file
 */
static FILE* prepareFileDescriptorForParsing(struct fileParams* fileSpec)
{
    /* try getting a valid file descriptor */
    FILE* fileDescriptor = fileSpec->fileDescriptor;
    if (NULL == fileDescriptor)
    {
        if (NULL != fileSpec->filename)
        {
            /* try opening the file */
            fileDescriptor = fopen(fileSpec->filename, "r");
            if (NULL == fileDescriptor)
            {
                return NULL;
            }
            else
            {
                fileSpec->fileDescriptor = fileDescriptor;
            }
        }
        else
        {
            return NULL;
        }
    }
    else
    {
        /* try to seek to the beginning of the file */
        int newOffset = fseek(fileDescriptor, 0, SEEK_SET);
        if (-1 == newOffset)
        {
            /* if seeking fails, do close and open the file again */
            log_error("Failed to reset the read offset using fseek.");
            fprintf(stderr, "File \"%s\" is affected. errno = %d\n", fileSpec->filename, errno);
            fclose(fileDescriptor);
            fileDescriptor = fopen(fileSpec->filename, "r");
            if (NULL == fileDescriptor)
            {
                return NULL;
            }
            else
            {
                fileSpec->fileDescriptor = fileDescriptor;
            }
        }
    }

    return fileDescriptor;
}

/**
 * Part of parseWholeFile, just process a line, adding the values of found field matches to the
 * result Vector foundValuesVec
 */
static void processLine(struct fileParams* fileSpec, int* varParamsIndex, int curLineNumber,
                        struct Vector* foundValuesVec, char* myLine, bool verbose)
{
    if (NULL == fileSpec || NULL == fileSpec->dataDefinitions || NULL == foundValuesVec)
    {
        return;
    }
    if (*varParamsIndex >= fileSpec->dataDefinitions->length)
    {
        return;
    }
    struct varParams* curVarSpec =
        (struct varParams*)fileSpec->dataDefinitions->data[*varParamsIndex];

    while (NULL != curVarSpec && curLineNumber == curVarSpec->posRow)
    {
        char separator[] = { 0, 0 };
        separator[0] = curVarSpec->posSep;
        char* lineDupForStrtok = strdup(myLine);
        char* nextToken = NULL;
        char* curToken = strtok_r(lineDupForStrtok, separator, &nextToken);
        int curColumnIndex = 0;
        while (NULL != curToken)
        {

            if (curColumnIndex == curVarSpec->posCol)
            {
                tryAppendingValueToFoundValuesVec(fileSpec, curVarSpec, foundValuesVec, curToken);
                break;
            }

            curToken = strtok_r(NULL, separator, &nextToken);
            ++curColumnIndex;
        }
        if(verbose)
        {
            if(NULL == curToken && curColumnIndex <= curVarSpec->posCol)
            {
                log_error_string("Could not read metric \"%s\", not enough columns in line", curVarSpec->name);
            }
        }
        *varParamsIndex = *varParamsIndex + 1;
        if (*varParamsIndex < fileSpec->dataDefinitions->length)
        {
            curVarSpec = (struct varParams*)fileSpec->dataDefinitions->data[*varParamsIndex];
        }
        else
        {
            curVarSpec = NULL;
        }
        free(lineDupForStrtok);
    }
}

/**
 * This function simply tries to append a freshly read out value to the resultset foundValuesVec
 */
static void tryAppendingValueToFoundValuesVec(struct fileParams* fileSpec,
                                              struct varParams* varSpec,
                                              struct Vector* foundValuesVec, char* foundStr)
{
    struct foundValue* found = calloc(1, sizeof(struct foundValue));
    if (NULL != found)
    {
		found->associatedVarParams = varSpec;
		found->associatedFileParams = fileSpec;
		if (0 < varSpec->inputBinaryWidth)
		{
			found->associatedValue =
					parseValueBinary(foundStr, varSpec->inputBinaryWidth, varSpec->binaryDatatype);
		}
		else
		{
			found->associatedValue = parseValue(foundStr, varSpec->datatype, varSpec->inputHex);
		}
		if (vec_append(foundValuesVec, found))
		{
			log_error("Could not append read value to foundValuesVec, insufficient memory.");
		}
    }
    else
    {
        log_error("Could not allocate memory for storing a parsed value (calloc error). ");
    }
}

/**
 * Function to be invoked by Scorep to give a bit of information on this plugin
 */
SCOREP_METRIC_PLUGIN_ENTRY(fileparser_plugin)
{
    SCOREP_Metric_Plugin_Info info;
    memset(&info, 0, sizeof(SCOREP_Metric_Plugin_Info));
    info.plugin_version = SCOREP_METRIC_PLUGIN_VERSION;
    info.run_per = SCOREP_METRIC_PER_HOST;
    info.sync = SCOREP_METRIC_ASYNC;
    info.delta_t = UINT64_MAX;
    info.initialize = init;
    info.finalize = fini;
    /* define callbacks */
    info.get_event_info = get_event_info;
    info.add_counter = add_counter;
    info.get_all_values = get_all_values;
    info.set_clock_function = set_timer;

    return info;
}

/**
 * Simplistic function to print out an error message
 */
static void log_error(char* errorMessage)
{
    fprintf(stderr, "Score-P Fileparser Plugin: %s\n", errorMessage);
}

/**
 * Simplistic function to print out an error message
 */
static void log_error_string(char* errorMessage, char* argumentToPrint)
{
    fprintf(stderr, "Score-P Fileparser Plugin: ");
    fprintf(stderr, errorMessage, argumentToPrint);
    fprintf(stderr, "\n");
}
