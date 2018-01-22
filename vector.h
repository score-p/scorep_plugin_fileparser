/*
 * vector.h
 *
 *  Created on: 13.11.2017
 *      Author: jitschin
 */

#ifndef VECTOR_H_
#define VECTOR_H_

#include <stdlib.h>


/* A container struct for holding an arbitrary amount of any kind of elements */
struct Vector
{
  int length; /**< How many elements are currently in use? */
  int reserve; /**< How large is the memory reserve */
  void** data; /**< An array of pointers to the associated data */
};

/**
 * Creates a new struct Vector and returns a pointer to it
 *
 * @param intended initialCapacity (i.e. how large the memory reserve is)
 * @return Returns NULL if not even a single byte of memory could be allocated
 */
struct Vector* vec_create(int initialCapacity);

/**
 * Appends a pointer to a struct Vector
 *
 * @param container is to be a pointer to a struct Vector
 * @param value may be a pointer to any data
 * @return Returns 0 on success, a value greater than that on error
 */
int vec_append(struct Vector* container, void* value);

/**
 * Destroys a given vector, i.e. doing a free on it's data and on the vector itself
 */
void vec_destroy(struct Vector* container);

#endif /* VECTOR_H_ */
