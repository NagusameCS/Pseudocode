/*
 * Pseudocode WASM Standard Library - HTTP Functions
 * 
 * HTTP operations using fetch API (works in both browser and Node.js 18+).
 */

import { 
    Value, valString, valInt, valObject, valNil,
    isString, isObject, asPointer, valToString
} from '../runtime/values';
import { Memory } from '../runtime/memory';

/**
 * Create HTTP functions for the runtime.
 */
export function createHttpFunctions(memory: Memory) {
    // Helper to extract headers from a dictionary value
    const extractHeaders = (headersVal: Value | undefined): Record<string, string> => {
        if (!headersVal || !isObject(headersVal)) {
            return {};
        }
        
        const ptr = asPointer(headersVal);
        const headers: Record<string, string> = {};
        
        for (const [key, value] of memory.dictEntries(ptr)) {
            headers[key] = isString(value) 
                ? memory.getString(asPointer(value)) 
                : valToString(value, memory);
        }
        
        return headers;
    };
    
    // Helper to create response dict
    const createResponse = (status: number, body: string, headers: Record<string, string>): Value => {
        const ptr = memory.allocDict();
        
        memory.dictSet(ptr, 'status', BigInt(status) << 3n | 2n); // valInt
        memory.dictSet(ptr, 'body', memory.allocString(body));
        memory.dictSet(ptr, 'ok', status >= 200 && status < 300 
            ? (1n << 3n | 1n) // valBool(true)
            : (0n << 3n | 1n) // valBool(false)
        );
        
        // Create headers dict
        const headersPtr = memory.allocDict();
        for (const [key, value] of Object.entries(headers)) {
            memory.dictSet(headersPtr, key.toLowerCase(), memory.allocString(value));
        }
        memory.dictSet(ptr, 'headers', valObject(headersPtr));
        
        return valObject(ptr);
    };
    
    return {
        /**
         * Perform HTTP GET request.
         */
        async http_get(url: Value, options?: Value): Promise<Value> {
            if (!isString(url)) {
                throw new Error('http_get() requires a string URL');
            }
            
            const urlStr = memory.getString(asPointer(url));
            const headers = options && isObject(options) 
                ? extractHeaders(memory.dictGet(asPointer(options), 'headers') as Value | undefined)
                : {};
            
            try {
                const response = await fetch(urlStr, {
                    method: 'GET',
                    headers
                });
                
                const body = await response.text();
                const responseHeaders: Record<string, string> = {};
                response.headers.forEach((value, key) => {
                    responseHeaders[key] = value;
                });
                
                return createResponse(response.status, body, responseHeaders);
            } catch (error) {
                throw new Error(`HTTP GET failed: ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Perform HTTP POST request.
         */
        async http_post(url: Value, body: Value, options?: Value): Promise<Value> {
            if (!isString(url)) {
                throw new Error('http_post() requires a string URL');
            }
            
            const urlStr = memory.getString(asPointer(url));
            const bodyStr = isString(body) 
                ? memory.getString(asPointer(body)) 
                : valToString(body, memory);
            
            const headers = options && isObject(options)
                ? extractHeaders(memory.dictGet(asPointer(options), 'headers') as Value | undefined)
                : {};
            
            // Default content-type for JSON
            if (!headers['content-type'] && !headers['Content-Type']) {
                headers['Content-Type'] = 'application/json';
            }
            
            try {
                const response = await fetch(urlStr, {
                    method: 'POST',
                    headers,
                    body: bodyStr
                });
                
                const responseBody = await response.text();
                const responseHeaders: Record<string, string> = {};
                response.headers.forEach((value, key) => {
                    responseHeaders[key] = value;
                });
                
                return createResponse(response.status, responseBody, responseHeaders);
            } catch (error) {
                throw new Error(`HTTP POST failed: ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Perform HTTP PUT request.
         */
        async http_put(url: Value, body: Value, options?: Value): Promise<Value> {
            if (!isString(url)) {
                throw new Error('http_put() requires a string URL');
            }
            
            const urlStr = memory.getString(asPointer(url));
            const bodyStr = isString(body) 
                ? memory.getString(asPointer(body)) 
                : valToString(body, memory);
            
            const headers = options && isObject(options)
                ? extractHeaders(memory.dictGet(asPointer(options), 'headers') as Value | undefined)
                : {};
            
            if (!headers['content-type'] && !headers['Content-Type']) {
                headers['Content-Type'] = 'application/json';
            }
            
            try {
                const response = await fetch(urlStr, {
                    method: 'PUT',
                    headers,
                    body: bodyStr
                });
                
                const responseBody = await response.text();
                const responseHeaders: Record<string, string> = {};
                response.headers.forEach((value, key) => {
                    responseHeaders[key] = value;
                });
                
                return createResponse(response.status, responseBody, responseHeaders);
            } catch (error) {
                throw new Error(`HTTP PUT failed: ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Perform HTTP PATCH request.
         */
        async http_patch(url: Value, body: Value, options?: Value): Promise<Value> {
            if (!isString(url)) {
                throw new Error('http_patch() requires a string URL');
            }
            
            const urlStr = memory.getString(asPointer(url));
            const bodyStr = isString(body) 
                ? memory.getString(asPointer(body)) 
                : valToString(body, memory);
            
            const headers = options && isObject(options)
                ? extractHeaders(memory.dictGet(asPointer(options), 'headers') as Value | undefined)
                : {};
            
            if (!headers['content-type'] && !headers['Content-Type']) {
                headers['Content-Type'] = 'application/json';
            }
            
            try {
                const response = await fetch(urlStr, {
                    method: 'PATCH',
                    headers,
                    body: bodyStr
                });
                
                const responseBody = await response.text();
                const responseHeaders: Record<string, string> = {};
                response.headers.forEach((value, key) => {
                    responseHeaders[key] = value;
                });
                
                return createResponse(response.status, responseBody, responseHeaders);
            } catch (error) {
                throw new Error(`HTTP PATCH failed: ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Perform HTTP DELETE request.
         */
        async http_delete(url: Value, options?: Value): Promise<Value> {
            if (!isString(url)) {
                throw new Error('http_delete() requires a string URL');
            }
            
            const urlStr = memory.getString(asPointer(url));
            const headers = options && isObject(options)
                ? extractHeaders(memory.dictGet(asPointer(options), 'headers') as Value | undefined)
                : {};
            
            try {
                const response = await fetch(urlStr, {
                    method: 'DELETE',
                    headers
                });
                
                const body = await response.text();
                const responseHeaders: Record<string, string> = {};
                response.headers.forEach((value, key) => {
                    responseHeaders[key] = value;
                });
                
                return createResponse(response.status, body, responseHeaders);
            } catch (error) {
                throw new Error(`HTTP DELETE failed: ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Perform generic HTTP request.
         */
        async http_request(url: Value, options: Value): Promise<Value> {
            if (!isString(url)) {
                throw new Error('http_request() requires a string URL');
            }
            if (!isObject(options)) {
                throw new Error('http_request() requires an options dictionary');
            }
            
            const urlStr = memory.getString(asPointer(url));
            const optionsPtr = asPointer(options);
            
            // Extract options
            const methodVal = memory.dictGet(optionsPtr, 'method');
            const method = methodVal && isString(methodVal) 
                ? memory.getString(asPointer(methodVal)).toUpperCase() 
                : 'GET';
            
            const headersVal = memory.dictGet(optionsPtr, 'headers');
            const headers = extractHeaders(headersVal as Value | undefined);
            
            const bodyVal = memory.dictGet(optionsPtr, 'body');
            const body = bodyVal && isString(bodyVal) 
                ? memory.getString(asPointer(bodyVal)) 
                : undefined;
            
            try {
                const fetchOptions: RequestInit = { method, headers };
                if (body && ['POST', 'PUT', 'PATCH'].includes(method)) {
                    fetchOptions.body = body;
                }
                
                const response = await fetch(urlStr, fetchOptions);
                
                const responseBody = await response.text();
                const responseHeaders: Record<string, string> = {};
                response.headers.forEach((value, key) => {
                    responseHeaders[key] = value;
                });
                
                return createResponse(response.status, responseBody, responseHeaders);
            } catch (error) {
                throw new Error(`HTTP request failed: ${error instanceof Error ? error.message : String(error)}`);
            }
        },
        
        /**
         * Simple GET that returns body string directly.
         */
        async fetch_text(url: Value): Promise<Value> {
            if (!isString(url)) {
                throw new Error('fetch_text() requires a string URL');
            }
            
            const urlStr = memory.getString(asPointer(url));
            
            try {
                const response = await fetch(urlStr);
                if (!response.ok) {
                    throw new Error(`HTTP ${response.status}: ${response.statusText}`);
                }
                const text = await response.text();
                return memory.allocString(text);
            } catch (error) {
                throw new Error(`fetch_text failed: ${error instanceof Error ? error.message : String(error)}`);
            }
        },
    };
}
