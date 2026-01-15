import { Value } from '../runtime/values';
import { Memory } from '../runtime/memory';
/**
 * Create JSON functions for the runtime.
 */
export declare function createJsonFunctions(memory: Memory): {
    /**
     * Parse JSON string to Pseudocode value.
     */
    json_parse(jsonStr: Value): Value;
    /**
     * Convert Pseudocode value to JSON string.
     */
    json_stringify(value: Value, pretty?: Value): Value;
    /**
     * Check if a string is valid JSON.
     */
    json_valid(jsonStr: Value): Value;
    /**
     * Get a value from JSON by path (e.g., "user.address.city" or "items[0].name").
     */
    json_get(value: Value, path: Value): Value;
    /**
     * Set a value in JSON by path.
     */
    json_set(value: Value, path: Value, newValue: Value): Value;
    /**
     * Merge multiple JSON objects.
     */
    json_merge(...values: Value[]): Value;
    /**
     * Deep clone a JSON value.
     */
    json_clone(value: Value): Value;
    /**
     * Compare two JSON values for deep equality.
     */
    json_equals(a: Value, b: Value): Value;
};
//# sourceMappingURL=json.d.ts.map