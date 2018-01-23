/*
 * measurement_blob.h
 *
 *  Created on: 04.01.2018
 *      Author: jitschin
 */

#ifndef MEASUREMENT_BLOB_H_
#define MEASUREMENT_BLOB_H_

/* required for datatype uint64_t */
#include <stdint.h>
/* required for 'NULL' */
#include <stdio.h>
/* required for stroll, stroull, strtod */
#include <stdlib.h>
/* required for strstr */
#include <string.h>
/* required for metric plugin */
#include <scorep/SCOREP_MetricPlugins.h>

/* A logging tuple/blob, a basic data structure to hold a single value */
struct measurement_blob
{
  uint64_t start_time; /**< timestamp at which time this value first occured */
  uint64_t value; /**< logged value */
};

/* A container for holding measurement_blobs */
struct blob_holder
{
  uint64_t length; /**< count of used elements in this container */
  uint64_t reserved; /**< how much space is allocated for measurement_blob elements in arr */
  uint64_t total_count_stored_values; /**< count of values that have been stored (including repetitions) */
  uint64_t initial_value; /**< the initial value that can be subtracted from each entry */
  struct measurement_blob* arr; /**< the array to hold the logged values */
};

typedef enum Fileparser_Binary_Datatype
{
  FILEPARSER_BINARY_DATATYPE_UNDEFINED,
  FILEPARSER_BINARY_DATATYPE_INT8,
  FILEPARSER_BINARY_DATATYPE_INT16,
  FILEPARSER_BINARY_DATATYPE_INT32,
  FILEPARSER_BINARY_DATATYPE_INT64,
  FILEPARSER_BINARY_DATATYPE_UINT8,
  FILEPARSER_BINARY_DATATYPE_UINT16,
  FILEPARSER_BINARY_DATATYPE_UINT32,
  FILEPARSER_BINARY_DATATYPE_UINT64,
  FILEPARSER_BINARY_DATATYPE_FLOAT,
  FILEPARSER_BINARY_DATATYPE_DOUBLE
} Fileparser_Binary_Datatype;


/**
 * Parses a given strValue according to given strDatatype, returns a uint64_t regardless if the parsed value is integer or floating point
 */
uint64_t parseValue(char* strValue, SCOREP_MetricValueType curDatatype, int isHex);

/**
 * Parses a given binValue according to binaryDatatype returning a uint64_t representation of the value
 */
uint64_t parseValueBinary(char* binValue, int inputBinaryWidth, Fileparser_Binary_Datatype binaryDatatype);

/**
 * Creates a new container with an initial_capacity and an initial_value (which is heeded later when calculating a dif)
 */
struct blob_holder* blobarray_create(uint64_t initial_capacity, uint64_t initial_value);

/**
 * Cleanup function to be used at the end of life of the container
 */
void blobarray_destroy_subelements(struct blob_holder* container);

/**
 * This function takes care of adding a log value to the given blob_holder, saving only a Dif value if necessary
 */
int blobarray_append(struct blob_holder* container, uint64_t value, uint64_t timestamp, int logDif, SCOREP_MetricValueType curDatatype);

/**
 * This function resets a given container to zero, so it can be reused without delay
 */
void blobarray_reset(struct blob_holder* container);

/**
 * sets return_reference to an array containing the logged data points, repetitions are omitted
 */
int blobarray_get_TimeValuePairs(struct blob_holder* container, SCOREP_MetricTimeValuePair** return_reference);



#endif /* MEASUREMENT_BLOB_H_ */
