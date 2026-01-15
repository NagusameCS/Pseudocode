/*
 * Pseudocode WASM Standard Library - Crypto Functions
 */

import { 
    Value, valString, isString, asPointer
} from '../runtime/values';
import { Memory } from '../runtime/memory';

/**
 * Create crypto functions for the runtime.
 */
export function createCryptoFunctions(memory: Memory) {
    // Base64 encoding table
    const BASE64_CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    
    // Helper to convert string to Uint8Array
    const stringToBytes = (str: string): Uint8Array => {
        const encoder = new TextEncoder();
        return encoder.encode(str);
    };
    
    // Helper to convert Uint8Array to string
    const bytesToString = (bytes: Uint8Array): string => {
        const decoder = new TextDecoder();
        return decoder.decode(bytes);
    };
    
    return {
        /**
         * Encode string to Base64.
         */
        encode_base64(data: Value): Value {
            if (!isString(data)) {
                throw new Error('encode_base64() requires a string');
            }
            
            const str = memory.getString(asPointer(data));
            
            // Use native btoa if available
            if (typeof btoa !== 'undefined') {
                // Handle unicode
                const bytes = stringToBytes(str);
                let binary = '';
                for (let i = 0; i < bytes.length; i++) {
                    binary += String.fromCharCode(bytes[i]);
                }
                return memory.allocString(btoa(binary));
            }
            
            // Fallback: manual base64 encoding
            const bytes = stringToBytes(str);
            let result = '';
            
            for (let i = 0; i < bytes.length; i += 3) {
                const a = bytes[i];
                const b = i + 1 < bytes.length ? bytes[i + 1] : 0;
                const c = i + 2 < bytes.length ? bytes[i + 2] : 0;
                
                result += BASE64_CHARS[a >> 2];
                result += BASE64_CHARS[((a & 3) << 4) | (b >> 4)];
                result += i + 1 < bytes.length ? BASE64_CHARS[((b & 15) << 2) | (c >> 6)] : '=';
                result += i + 2 < bytes.length ? BASE64_CHARS[c & 63] : '=';
            }
            
            return memory.allocString(result);
        },
        
        /**
         * Decode Base64 to string.
         */
        decode_base64(data: Value): Value {
            if (!isString(data)) {
                throw new Error('decode_base64() requires a string');
            }
            
            const str = memory.getString(asPointer(data));
            
            // Use native atob if available
            if (typeof atob !== 'undefined') {
                try {
                    const binary = atob(str);
                    const bytes = new Uint8Array(binary.length);
                    for (let i = 0; i < binary.length; i++) {
                        bytes[i] = binary.charCodeAt(i);
                    }
                    return memory.allocString(bytesToString(bytes));
                } catch {
                    throw new Error('Invalid Base64 string');
                }
            }
            
            // Fallback: manual base64 decoding
            const lookup = new Map<string, number>();
            for (let i = 0; i < BASE64_CHARS.length; i++) {
                lookup.set(BASE64_CHARS[i], i);
            }
            
            // Remove padding
            let input = str.replace(/=+$/, '');
            const bytes: number[] = [];
            
            for (let i = 0; i < input.length; i += 4) {
                const a = lookup.get(input[i]) ?? 0;
                const b = lookup.get(input[i + 1]) ?? 0;
                const c = lookup.get(input[i + 2]) ?? 0;
                const d = lookup.get(input[i + 3]) ?? 0;
                
                bytes.push((a << 2) | (b >> 4));
                if (i + 2 < input.length) bytes.push(((b & 15) << 4) | (c >> 2));
                if (i + 3 < input.length) bytes.push(((c & 3) << 6) | d);
            }
            
            return memory.allocString(bytesToString(new Uint8Array(bytes)));
        },
        
        /**
         * Calculate SHA-256 hash (async in browser, sync in Node).
         */
        async sha256(data: Value): Promise<Value> {
            if (!isString(data)) {
                throw new Error('sha256() requires a string');
            }
            
            const str = memory.getString(asPointer(data));
            const bytes = stringToBytes(str);
            
            // Try Web Crypto API
            if (typeof crypto !== 'undefined' && crypto.subtle) {
                const hashBuffer = await crypto.subtle.digest('SHA-256', bytes.buffer as ArrayBuffer);
                const hashArray = new Uint8Array(hashBuffer);
                const hashHex = Array.from(hashArray)
                    .map(b => b.toString(16).padStart(2, '0'))
                    .join('');
                return memory.allocString(hashHex);
            }
            
            // Fallback: try Node.js crypto
            try {
                // eslint-disable-next-line @typescript-eslint/no-var-requires
                const nodeCrypto = require('crypto');
                const hash = nodeCrypto.createHash('sha256').update(str).digest('hex');
                return memory.allocString(hash);
            } catch {
                throw new Error('SHA-256 not available in this environment');
            }
        },
        
        /**
         * Calculate MD5 hash (for legacy compatibility).
         */
        async md5(data: Value): Promise<Value> {
            if (!isString(data)) {
                throw new Error('md5() requires a string');
            }
            
            const str = memory.getString(asPointer(data));
            
            // Try Node.js crypto
            try {
                // eslint-disable-next-line @typescript-eslint/no-var-requires
                const nodeCrypto = require('crypto');
                const hash = nodeCrypto.createHash('md5').update(str).digest('hex');
                return memory.allocString(hash);
            } catch {
                // MD5 not available in browser Web Crypto API
                throw new Error('MD5 not available in this environment (use sha256 instead)');
            }
        },
        
        /**
         * Generate a UUID v4.
         */
        uuid(): Value {
            // Use crypto.randomUUID if available
            if (typeof crypto !== 'undefined' && crypto.randomUUID) {
                return memory.allocString(crypto.randomUUID());
            }
            
            // Fallback: manual UUID generation
            const bytes = new Uint8Array(16);
            if (typeof crypto !== 'undefined' && crypto.getRandomValues) {
                crypto.getRandomValues(bytes);
            } else {
                for (let i = 0; i < 16; i++) {
                    bytes[i] = Math.floor(Math.random() * 256);
                }
            }
            
            // Set version (4) and variant (8, 9, A, or B)
            bytes[6] = (bytes[6] & 0x0f) | 0x40;
            bytes[8] = (bytes[8] & 0x3f) | 0x80;
            
            const hex = Array.from(bytes)
                .map(b => b.toString(16).padStart(2, '0'))
                .join('');
            
            const uuid = [
                hex.slice(0, 8),
                hex.slice(8, 12),
                hex.slice(12, 16),
                hex.slice(16, 20),
                hex.slice(20, 32)
            ].join('-');
            
            return memory.allocString(uuid);
        },
        
        /**
         * URL-encode a string.
         */
        url_encode(data: Value): Value {
            if (!isString(data)) {
                throw new Error('url_encode() requires a string');
            }
            
            const str = memory.getString(asPointer(data));
            return memory.allocString(encodeURIComponent(str));
        },
        
        /**
         * URL-decode a string.
         */
        url_decode(data: Value): Value {
            if (!isString(data)) {
                throw new Error('url_decode() requires a string');
            }
            
            const str = memory.getString(asPointer(data));
            try {
                return memory.allocString(decodeURIComponent(str));
            } catch {
                throw new Error('Invalid URL-encoded string');
            }
        },
        
        /**
         * Hex encode a string.
         */
        hex_encode(data: Value): Value {
            if (!isString(data)) {
                throw new Error('hex_encode() requires a string');
            }
            
            const str = memory.getString(asPointer(data));
            const bytes = stringToBytes(str);
            const hex = Array.from(bytes)
                .map(b => b.toString(16).padStart(2, '0'))
                .join('');
            return memory.allocString(hex);
        },
        
        /**
         * Hex decode a string.
         */
        hex_decode(data: Value): Value {
            if (!isString(data)) {
                throw new Error('hex_decode() requires a string');
            }
            
            const str = memory.getString(asPointer(data));
            if (str.length % 2 !== 0) {
                throw new Error('Invalid hex string (odd length)');
            }
            
            const bytes = new Uint8Array(str.length / 2);
            for (let i = 0; i < str.length; i += 2) {
                const byte = parseInt(str.slice(i, i + 2), 16);
                if (isNaN(byte)) {
                    throw new Error(`Invalid hex character at position ${i}`);
                }
                bytes[i / 2] = byte;
            }
            
            return memory.allocString(bytesToString(bytes));
        },
    };
}
