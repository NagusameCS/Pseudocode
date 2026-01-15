import { Value } from '../runtime/values';
import { Memory } from '../runtime/memory';
/**
 * Create dictionary functions for the runtime.
 */
export declare function createDictFunctions(memory: Memory, callFunction: (funcIdx: number, ...args: Value[]) => Value): {
    /**
     * Create a new empty dictionary.
     */
    dict_new(): Value;
    /**
     * Get a value from dictionary by key.
     */
    dict_get(dict: Value, key: Value, defaultValue?: Value): Value;
    /**
     * Set a value in dictionary.
     */
    dict_set(dict: Value, key: Value, value: Value): Value;
    /**
     * Delete a key from dictionary.
     */
    dict_delete(dict: Value, key: Value): Value;
    /**
     * Check if dictionary has a key.
     */
    has_key(dict: Value, key: Value): Value;
    /**
     * Get all keys from dictionary.
     */
    keys(dict: Value): Value;
    /**
     * Get all values from dictionary.
     */
    values(dict: Value): Value;
    /**
     * Get all entries from dictionary as [key, value] pairs.
     */
    entries(dict: Value): Value;
    /**
     * Get dictionary size.
     */
    dict_size(dict: Value): Value;
    /**
     * Clear all entries from dictionary.
     */
    dict_clear(dict: Value): Value;
    /**
     * Merge two dictionaries (second overwrites first).
     */
    dict_merge(dict1: Value, dict2: Value): Value;
    /**
     * Update dictionary in place with entries from another.
     */
    dict_update(dict: Value, other: Value): Value;
    /**
     * Create dictionary from array of [key, value] pairs.
     */
    dict_from_entries(entries: Value): Value;
    /**
     * Map function over dictionary values.
     */
    dict_map(dict: Value, func: Value): Value;
    /**
     * Filter dictionary by predicate.
     */
    dict_filter(dict: Value, predicate: Value): Value;
};
//# sourceMappingURL=dict.d.ts.map