import { Value } from '../runtime/values';
import { Memory } from '../runtime/memory';
/**
 * Create HTTP functions for the runtime.
 */
export declare function createHttpFunctions(memory: Memory): {
    /**
     * Perform HTTP GET request.
     */
    http_get(url: Value, options?: Value): Promise<Value>;
    /**
     * Perform HTTP POST request.
     */
    http_post(url: Value, body: Value, options?: Value): Promise<Value>;
    /**
     * Perform HTTP PUT request.
     */
    http_put(url: Value, body: Value, options?: Value): Promise<Value>;
    /**
     * Perform HTTP PATCH request.
     */
    http_patch(url: Value, body: Value, options?: Value): Promise<Value>;
    /**
     * Perform HTTP DELETE request.
     */
    http_delete(url: Value, options?: Value): Promise<Value>;
    /**
     * Perform generic HTTP request.
     */
    http_request(url: Value, options: Value): Promise<Value>;
    /**
     * Simple GET that returns body string directly.
     */
    fetch_text(url: Value): Promise<Value>;
};
//# sourceMappingURL=http.d.ts.map