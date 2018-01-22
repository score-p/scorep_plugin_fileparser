/*
 * measurement_blob.c
 *
 *  Created on: 04.01.2018
 *      Author: jitschin
 */


#include "measurement_blob.h"

/**
 * Utility function to allocate another few bytes of memory
 */
static int blobarray_allocate_for_more(struct blob_holder* container);

/**
 * If necessary calculate the diff to an initial value
 */
static uint64_t figure_out_actual_value(uint64_t cur_value, uint64_t initial_value, int logDif, SCOREP_MetricValueType curDatatype);

uint64_t parseValue(char* strValue, SCOREP_MetricValueType curDatatype, int isHex)
{
  /* return value is supposed to be a unsigned 64-bit integer regardless if a floating point or an integer is stored */
  union {
    double dbl;
    uint64_t uint;
    int64_t sint;
  } value;
  /* parse the read value according to datatype */
  if (NULL != strValue)
  {
    int conversionBase = 0;
    if(isHex)
    {
      if(strstr(strValue, "0x") == strValue)
      {
        strValue = strValue + 2;
      }
      conversionBase = 16;
    }
    char* endptr = NULL;
    switch(curDatatype)
    {
      case SCOREP_METRIC_VALUE_UINT64:
        value.uint = strtoull(strValue, &endptr, conversionBase);
        break;
      case SCOREP_METRIC_VALUE_DOUBLE:
        value.dbl = strtod(strValue, &endptr);
        break;
      case SCOREP_METRIC_VALUE_INT64: /* fall-through */
      default:
        value.sint = strtoll(strValue, &endptr, conversionBase);
        break;
    }


  } else {
    value.uint = 0;
  }
  return value.uint;
}

uint64_t parseValueBinary(char* binValue, int inputBinaryWidth, Netstats_Binary_Datatype binaryDatatype)
{
  if(NULL == binValue)
  {
    return 0;
  } else {
    union {
      double dbl;
      uint64_t uint;
      int64_t sint;
    } value;
    float cpyFloat = 0;
    int8_t sint8Val = 0;
    int16_t sint16Val = 0;
    int32_t sint32Val = 0;
    uint8_t uint8Val = 0;
    uint16_t uint16Val = 0;
    uint32_t uint32Val = 0;
    /* TODO: check if memcpy(sint, binValue, 1) really results in a value 0-255 or if it instead results in a value from 4 billion to 16 million */
    switch(binaryDatatype)
    {
      case NETSTATS_BINARY_DATATYPE_INT8:
        memcpy(&sint8Val, binValue, 1);
        value.sint = sint8Val;
        break;
      case NETSTATS_BINARY_DATATYPE_INT16:
        memcpy(&sint16Val, binValue, 2);
        value.sint = sint16Val;
        break;
      case NETSTATS_BINARY_DATATYPE_INT32:
        /* why does this line SEGFAULT?
         *memcpy(&sint32Val, binValue, 4);
         */
        memcpy(&sint32Val, binValue, sizeof(sint32Val));
        value.sint = sint32Val;
        break;
      case NETSTATS_BINARY_DATATYPE_INT64:
        memcpy(&(value.sint), binValue, inputBinaryWidth);
        break;
      case NETSTATS_BINARY_DATATYPE_UINT8:
        memcpy(&uint8Val, binValue, 1);
        value.uint = uint8Val;
        break;
      case NETSTATS_BINARY_DATATYPE_UINT16:
        memcpy(&uint16Val, binValue, 2);
        value.uint = uint16Val;
        break;
      case NETSTATS_BINARY_DATATYPE_UINT32:
        memcpy(&uint32Val, binValue, 4);
        value.uint = uint32Val;
        break;
      case NETSTATS_BINARY_DATATYPE_UINT64:
        memcpy(&(value.uint), binValue, inputBinaryWidth);
        break;
      case NETSTATS_BINARY_DATATYPE_FLOAT:
        memcpy(&cpyFloat, binValue, inputBinaryWidth);
        value.dbl = cpyFloat;
        break;
      case NETSTATS_BINARY_DATATYPE_DOUBLE:
        memcpy(&(value.dbl), binValue, inputBinaryWidth);
        break;
      case NETSTATS_BINARY_DATATYPE_UNDEFINED:
        memcpy(&sint8Val, binValue, 1);
        value.sint = sint8Val;
        break;
    }

    return value.uint;
  }
}

struct blob_holder* blobarray_create(uint64_t initial_capacity, uint64_t initial_value)
{
  /* try a calloc */
  struct blob_holder* container = calloc(1, sizeof(struct blob_holder));
  if(NULL != container)
  {
    container->initial_value = initial_value;
    /* try allocating memory for the array that is to be held */
    container->arr = calloc(initial_capacity, sizeof(struct measurement_blob));
    if(NULL == container->arr) {
      container->arr = calloc(1, sizeof(struct measurement_blob));
      if(NULL != container->arr)
      {
        container->reserved = 1;
      }
    } else {
      container->reserved = initial_capacity;
    }
  }
  return container;
}

void blobarray_destroy_subelements(struct blob_holder* container)
{
  if(NULL != container)
  {
    container->length = 0;
    container->reserved = 0;
    container->total_count_stored_values = 0;
    free(container->arr);
  }
}

int blobarray_append(struct blob_holder* container, uint64_t value, uint64_t timestamp, int logDif, SCOREP_MetricValueType curDatatype)
{
  struct measurement_blob* latest_blob = NULL;
  int isARepetition = 0;
  /* subtract initial_value from the provided value if necessary */
  uint64_t to_be_entered_value = figure_out_actual_value(value, container->initial_value, logDif, curDatatype);

  if(0 < container->length)
  {
    latest_blob = container->arr + (container->length - 1);
  }
  if(NULL != latest_blob)
  {
    /* the provided value was already stored in the previous element, just increase repetition counter */
    if(latest_blob->value == to_be_entered_value)
    {
      container->total_count_stored_values++;
      isARepetition = 1;
    }
  }
  if(0 == isARepetition)
  {
    /* make sure there is space for at least one more entry */
    if(blobarray_allocate_for_more(container))
    {
      return 1;
    }

    /* set a new entry in the logging array */
    container->arr[container->length].start_time = timestamp;
    container->arr[container->length].value = to_be_entered_value;
    container->total_count_stored_values++;
    container->length++;
  }
  return 0;
}

static uint64_t figure_out_actual_value(uint64_t cur_value, uint64_t initial_value, int logDif, SCOREP_MetricValueType curDatatype)
{
  if(0 == logDif) {
    return cur_value;
  }

  /* create unions for datatype specific subtracting */
  union {
    double dbl;
    uint64_t uint;
    int64_t sint;
  } u_cur_value;
  union {
    double dbl;
    uint64_t uint;
    int64_t sint;
  } u_initial_value;
  u_cur_value.uint = cur_value;
  u_initial_value.uint = initial_value;

  /* be carefull when subtracting different datatypes */
  switch(curDatatype)
  {
    case SCOREP_METRIC_VALUE_UINT64:
      u_cur_value.uint = u_cur_value.uint - u_initial_value.uint;
      break;
    case SCOREP_METRIC_VALUE_DOUBLE:
      u_cur_value.dbl = u_cur_value.dbl - u_initial_value.dbl;
      break;
    case SCOREP_METRIC_VALUE_INT64: /* fall-through */
    default:
      u_cur_value.sint = u_cur_value.sint - u_initial_value.sint;
      break;
  }

  return u_cur_value.uint;

}

int blobarray_get_TimeValuePairs(struct blob_holder* container, SCOREP_MetricTimeValuePair** return_reference)
{
  if(NULL == container)
  {
    return -1;
  }
  /* allocate memory for providing the values to Score-P */
  int to_be_allocated_count = container->length;
  SCOREP_MetricTimeValuePair* allocated_pairs = malloc(sizeof(SCOREP_MetricTimeValuePair) * to_be_allocated_count);
  if(NULL == allocated_pairs)
  {
    return -1;
  } else
  {
    /* run through the logged data, and put the data points into the array for Score-P */
    int total_index = 0;
    for (; total_index < container->length; ++total_index)
    {
      allocated_pairs[total_index].timestamp = container->arr[total_index].start_time;
      allocated_pairs[total_index].value = container->arr[total_index].value;
    }
    return_reference[0] = allocated_pairs;
    return total_index;
  }
}

void blobarray_reset(struct blob_holder* container)
{
  container->length = 0;
  container->total_count_stored_values = 0;
}

static int blobarray_allocate_for_more(struct blob_holder* container)
{
  if(NULL == container)
  {
    return 1;
  }
  /* if container is too small to fit another element, do some allocation */
  if((container->length + 1) >= container->reserved)
  {
    /* assume we want a new reserve of twice the previous size */
    int newReserve = container->reserved;
    if(2 > newReserve) {
      newReserve = 2;
    }
    /* try realloc of the container's logging array */
    struct measurement_blob* reallocSwap = realloc(container->arr, newReserve * sizeof(struct measurement_blob));
    if(NULL != reallocSwap)
    {
      container->arr = reallocSwap;
      container->reserved = newReserve;
    } else
    {
      /* if previous realloc failed, just try to realloc another two elements, that should be tiny enough */
      newReserve = container->reserved + 2;
      reallocSwap = realloc(container->arr, newReserve * sizeof(struct measurement_blob));
      if(NULL != reallocSwap)
      {
        container->arr = reallocSwap;
        container->reserved = newReserve;
      } else
      {
        fprintf(stderr,"Insufficient memory, could not allocate %d bytes for measurement_blob", (int) (2 * sizeof(struct measurement_blob)));
        return 2;
      }
    }
  }
  return 0;
}
