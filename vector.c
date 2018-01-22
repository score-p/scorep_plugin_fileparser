/*
 * vector.c
 *
 *  Created on: 30.10.2017
 *      Author: jitschin
 */
#include "vector.h"

/* required for calloc, realloc */
#include <stdlib.h>

struct Vector* vec_create(int initialCapacity)
{
  if(1 > initialCapacity)
  {
    initialCapacity = 1;
  }
  struct Vector* newVector = calloc(1, sizeof(struct Vector));
  if(NULL == newVector)
  {
    return NULL;
  }
  newVector->data = calloc(1, sizeof(void*) * initialCapacity);
  /* if allocation fails, try to allocate at least 1 byte*/
  if(NULL == newVector->data)
  {
    newVector->data = calloc(1, sizeof(void*) * 1);
    if(NULL == newVector->data)
    {
      free(newVector);
      return NULL;
    } else
    {
      newVector->reserve = 1;
    }
  } else
  {
    newVector->reserve = initialCapacity;
  }

  return newVector;
}


int vec_append(struct Vector* container, void* value)
{
  if(NULL == container)
  {
    return 1;
  }

  if((container->length + 1) > container->reserve)
  {
    int newReserve = container->reserve * 2;
    if(newReserve < 1)
    {
      newReserve = 1;
    }
    void** reallocArr = realloc(container->data, newReserve * sizeof(void*));
    if(NULL == reallocArr)
    {
      return 2;
    }
    container->reserve = newReserve;
    container->data = reallocArr;
  }
  container->data[container->length] = value;
  ++(container->length);

  return 0;
}


void vec_destroy(struct Vector* container)
{
  if(NULL == container)
  {
    return;
  }
  free(container->data);
  free(container);
}

