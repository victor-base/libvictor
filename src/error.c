#include "victor.h"
/**
 * victor_strerror - Returns a human-readable error message for an ErrorCode.
 *
 * This function maps each error code to a descriptive string suitable
 * for logs, stderr, or user-facing error messages.
 *
 * @param code  The ErrorCode value.
 *
 * @return A constant string with the error description.
 */
const char *victor_strerror(ErrorCode code) {
    switch (code) {
        case SUCCESS:             return "Operation completed successfully.";
        case INVALID_INIT:        return "Index not properly initialized.";
        case INVALID_INDEX:       return "Invalid index reference.";
        case INVALID_VECTOR:      return "Invalid vector provided.";
        case INVALID_RESULT:      return "Result buffer is invalid or uninitialized.";
        case INVALID_DIMENSIONS:  return "Vector has incompatible or invalid dimensions.";
        case INVALID_ARGUMENT:    return "Invalid argument passed to function.";
        case INVALID_ID:          return "Invalid or malformed ID.";
        case INVALID_REF:         return "Invalid pointer or reference.";
        case INVALID_METHOD:      return "Unknown or unsupported method.";
        case DUPLICATED_ENTRY:    return "Entry with this ID already exists.";
        case NOT_FOUND_ID:        return "No entry found with the specified ID.";
        case INDEX_EMPTY:         return "Index is empty.";
        case THREAD_ERROR:        return "Threading error or concurrency failure.";
        case SYSTEM_ERROR:        return "System-level error (e.g. memory, allocation).";
        case FILEIO_ERROR:        return "File I/O error.";
        case NOT_IMPLEMENTED:     return "Functionality not yet implemented.";
        case INVALID_FILE:        return "File format or contents are invalid.";
        default:                  return "Unknown error code.";
    }
}