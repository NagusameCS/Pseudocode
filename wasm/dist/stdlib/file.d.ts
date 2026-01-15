import { Value } from '../runtime/values';
import { Memory } from '../runtime/memory';
/**
 * Create file I/O functions for the runtime.
 */
export declare function createFileFunctions(memory: Memory): {
    /**
     * Read entire file contents as string.
     */
    read_file(path: Value): Value;
    /**
     * Write string content to file.
     */
    write_file(path: Value, content: Value): Value;
    /**
     * Append string content to file.
     */
    append_file(path: Value, content: Value): Value;
    /**
     * Check if file exists.
     */
    file_exists(path: Value): Value;
    /**
     * Delete a file.
     */
    delete_file(path: Value): Value;
    /**
     * Create a directory.
     */
    mkdir(path: Value, recursive?: Value): Value;
    /**
     * Remove a directory.
     */
    rmdir(path: Value, recursive?: Value): Value;
    /**
     * List directory contents.
     */
    list_dir(path: Value): Value;
    /**
     * Check if path is a file.
     */
    is_file(path: Value): Value;
    /**
     * Check if path is a directory.
     */
    is_dir(path: Value): Value;
    /**
     * Get file size in bytes.
     */
    file_size(path: Value): Value;
    /**
     * Copy a file.
     */
    copy_file(src: Value, dest: Value): Value;
    /**
     * Move/rename a file.
     */
    move_file(src: Value, dest: Value): Value;
};
//# sourceMappingURL=file.d.ts.map