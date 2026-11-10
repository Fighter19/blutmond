#pragma once

typedef enum BmError {
  BM_SUCCESS = 0,
  BM_ERROR_OUT_OF_MEMORY,
  BM_ERROR_INVALID_ARGUMENT,
  BM_ERROR_TOO_MANY_OBJECTS,
} BmError;
