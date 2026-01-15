import { Value } from '../runtime/values';
import { Memory } from '../runtime/memory';
/**
 * Create crypto functions for the runtime.
 */
export declare function createCryptoFunctions(memory: Memory): {
    /**
     * Encode string to Base64.
     */
    encode_base64(data: Value): Value;
    /**
     * Decode Base64 to string.
     */
    decode_base64(data: Value): Value;
    /**
     * Calculate SHA-256 hash (async in browser, sync in Node).
     */
    sha256(data: Value): Promise<Value>;
    /**
     * Calculate MD5 hash (for legacy compatibility).
     */
    md5(data: Value): Promise<Value>;
    /**
     * Generate a UUID v4.
     */
    uuid(): Value;
    /**
     * URL-encode a string.
     */
    url_encode(data: Value): Value;
    /**
     * URL-decode a string.
     */
    url_decode(data: Value): Value;
    /**
     * Hex encode a string.
     */
    hex_encode(data: Value): Value;
    /**
     * Hex decode a string.
     */
    hex_decode(data: Value): Value;
};
//# sourceMappingURL=crypto.d.ts.map