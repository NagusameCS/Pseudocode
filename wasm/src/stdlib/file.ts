/*
 * Pseudocode WASM Standard Library - File I/O Functions
 * 
 * File operations are only available in Node.js environments.
 * In browser environments, these will throw errors.
 */

import { 
    Value, valString, valBool, valNil, valArray, valInt,
    isString, asPointer, valToString
} from '../runtime/values';
import { Memory } from '../runtime/memory';

// Type declarations for Node.js fs module
interface NodeFS {
    readFileSync(path: string, encoding: string): string;
    writeFileSync(path: string, data: string): void;
    appendFileSync(path: string, data: string): void;
    existsSync(path: string): boolean;
    unlinkSync(path: string): void;
    mkdirSync(path: string, options?: { recursive?: boolean }): void;
    rmdirSync(path: string, options?: { recursive?: boolean }): void;
    readdirSync(path: string): string[];
    statSync(path: string): { isFile(): boolean; isDirectory(): boolean; size: number; mtime: Date };
    copyFileSync(src: string, dest: string): void;
    renameSync(oldPath: string, newPath: string): void;
}

// Dynamic require for Node.js
let fs: NodeFS | null = null;
try {
    // eslint-disable-next-line @typescript-eslint/no-var-requires
    fs = require('fs') as NodeFS;
} catch {
    // Not in Node.js environment
}

/**
 * Create file I/O functions for the runtime.
 */
export function createFileFunctions(memory: Memory) {
    const checkNodeEnvironment = () => {
        if (!fs) {
            throw new Error('File operations are only available in Node.js environments');
        }
    };
    
    return {
        /**
         * Read entire file contents as string.
         */
        read_file(path: Value): Value {
            checkNodeEnvironment();
            
            if (!isString(path)) {
                throw new Error('read_file() requires a string path');
            }
            
            const pathStr = memory.getString(asPointer(path));
            try {
                const content = fs!.readFileSync(pathStr, 'utf8');
                return memory.allocString(content);
            } catch (error) {
                throw new Error(`Failed to read file '${pathStr}': ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Write string content to file.
         */
        write_file(path: Value, content: Value): Value {
            checkNodeEnvironment();
            
            if (!isString(path)) {
                throw new Error('write_file() requires a string path');
            }
            
            const pathStr = memory.getString(asPointer(path));
            const contentStr = isString(content) 
                ? memory.getString(asPointer(content)) 
                : valToString(content, memory);
            
            try {
                fs!.writeFileSync(pathStr, contentStr);
                return valBool(true);
            } catch (error) {
                throw new Error(`Failed to write file '${pathStr}': ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Append string content to file.
         */
        append_file(path: Value, content: Value): Value {
            checkNodeEnvironment();
            
            if (!isString(path)) {
                throw new Error('append_file() requires a string path');
            }
            
            const pathStr = memory.getString(asPointer(path));
            const contentStr = isString(content) 
                ? memory.getString(asPointer(content)) 
                : valToString(content, memory);
            
            try {
                fs!.appendFileSync(pathStr, contentStr);
                return valBool(true);
            } catch (error) {
                throw new Error(`Failed to append to file '${pathStr}': ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Check if file exists.
         */
        file_exists(path: Value): Value {
            checkNodeEnvironment();
            
            if (!isString(path)) {
                throw new Error('file_exists() requires a string path');
            }
            
            const pathStr = memory.getString(asPointer(path));
            return valBool(fs!.existsSync(pathStr));
        },
        
        /**
         * Delete a file.
         */
        delete_file(path: Value): Value {
            checkNodeEnvironment();
            
            if (!isString(path)) {
                throw new Error('delete_file() requires a string path');
            }
            
            const pathStr = memory.getString(asPointer(path));
            try {
                fs!.unlinkSync(pathStr);
                return valBool(true);
            } catch (error) {
                throw new Error(`Failed to delete file '${pathStr}': ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Create a directory.
         */
        mkdir(path: Value, recursive?: Value): Value {
            checkNodeEnvironment();
            
            if (!isString(path)) {
                throw new Error('mkdir() requires a string path');
            }
            
            const pathStr = memory.getString(asPointer(path));
            const isRecursive = recursive ? Boolean(recursive) : false;
            
            try {
                fs!.mkdirSync(pathStr, { recursive: isRecursive });
                return valBool(true);
            } catch (error) {
                throw new Error(`Failed to create directory '${pathStr}': ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Remove a directory.
         */
        rmdir(path: Value, recursive?: Value): Value {
            checkNodeEnvironment();
            
            if (!isString(path)) {
                throw new Error('rmdir() requires a string path');
            }
            
            const pathStr = memory.getString(asPointer(path));
            const isRecursive = recursive ? Boolean(recursive) : false;
            
            try {
                fs!.rmdirSync(pathStr, { recursive: isRecursive });
                return valBool(true);
            } catch (error) {
                throw new Error(`Failed to remove directory '${pathStr}': ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * List directory contents.
         */
        list_dir(path: Value): Value {
            checkNodeEnvironment();
            
            if (!isString(path)) {
                throw new Error('list_dir() requires a string path');
            }
            
            const pathStr = memory.getString(asPointer(path));
            try {
                const entries = fs!.readdirSync(pathStr);
                const arr = memory.allocArray(entries.length);
                for (const entry of entries) {
                    memory.arrayPush(arr, memory.allocString(entry));
                }
                return valArray(arr);
            } catch (error) {
                throw new Error(`Failed to list directory '${pathStr}': ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Check if path is a file.
         */
        is_file(path: Value): Value {
            checkNodeEnvironment();
            
            if (!isString(path)) {
                throw new Error('is_file() requires a string path');
            }
            
            const pathStr = memory.getString(asPointer(path));
            try {
                return valBool(fs!.statSync(pathStr).isFile());
            } catch {
                return valBool(false);
            }
        },
        
        /**
         * Check if path is a directory.
         */
        is_dir(path: Value): Value {
            checkNodeEnvironment();
            
            if (!isString(path)) {
                throw new Error('is_dir() requires a string path');
            }
            
            const pathStr = memory.getString(asPointer(path));
            try {
                return valBool(fs!.statSync(pathStr).isDirectory());
            } catch {
                return valBool(false);
            }
        },
        
        /**
         * Get file size in bytes.
         */
        file_size(path: Value): Value {
            checkNodeEnvironment();
            
            if (!isString(path)) {
                throw new Error('file_size() requires a string path');
            }
            
            const pathStr = memory.getString(asPointer(path));
            try {
                return valInt(fs!.statSync(pathStr).size);
            } catch (error) {
                throw new Error(`Failed to get file size '${pathStr}': ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Copy a file.
         */
        copy_file(src: Value, dest: Value): Value {
            checkNodeEnvironment();
            
            if (!isString(src) || !isString(dest)) {
                throw new Error('copy_file() requires string paths');
            }
            
            const srcStr = memory.getString(asPointer(src));
            const destStr = memory.getString(asPointer(dest));
            
            try {
                fs!.copyFileSync(srcStr, destStr);
                return valBool(true);
            } catch (error) {
                throw new Error(`Failed to copy file '${srcStr}' to '${destStr}': ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Move/rename a file.
         */
        move_file(src: Value, dest: Value): Value {
            checkNodeEnvironment();
            
            if (!isString(src) || !isString(dest)) {
                throw new Error('move_file() requires string paths');
            }
            
            const srcStr = memory.getString(asPointer(src));
            const destStr = memory.getString(asPointer(dest));
            
            try {
                fs!.renameSync(srcStr, destStr);
                return valBool(true);
            } catch (error) {
                throw new Error(`Failed to move file '${srcStr}' to '${destStr}': ${error instanceof Error ? error.message : String(error)}`);
            }
        },
    };
}
