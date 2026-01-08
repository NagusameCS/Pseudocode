"""
Pseudocode Language - Lexer/Tokenizer
High-performance tokenization with minimal allocations
"""

from enum import IntEnum, auto
from dataclasses import dataclass
from typing import List, Optional
import re

class TokenType(IntEnum):
    # Literals
    INT = auto()
    FLOAT = auto()
    STRING = auto()
    TRUE = auto()
    FALSE = auto()
    
    # Identifiers & Keywords
    IDENT = auto()
    LET = auto()
    CONST = auto()
    FN = auto()
    RETURN = auto()
    IF = auto()
    THEN = auto()
    ELIF = auto()
    ELSE = auto()
    END = auto()
    WHILE = auto()
    FOR = auto()
    IN = auto()
    DO = auto()
    AND = auto()
    OR = auto()
    NOT = auto()
    
    # Operators
    PLUS = auto()
    MINUS = auto()
    STAR = auto()
    SLASH = auto()
    PERCENT = auto()
    EQ = auto()
    NEQ = auto()
    LT = auto()
    GT = auto()
    LTE = auto()
    GTE = auto()
    ASSIGN = auto()
    ARROW = auto()
    RANGE = auto()
    
    # Bitwise
    BAND = auto()
    BOR = auto()
    BXOR = auto()
    SHL = auto()
    SHR = auto()
    
    # Delimiters
    LPAREN = auto()
    RPAREN = auto()
    LBRACKET = auto()
    RBRACKET = auto()
    COMMA = auto()
    COLON = auto()
    NEWLINE = auto()
    
    # Special
    EOF = auto()
    ERROR = auto()

KEYWORDS = {
    'let': TokenType.LET,
    'const': TokenType.CONST,
    'fn': TokenType.FN,
    'return': TokenType.RETURN,
    'if': TokenType.IF,
    'then': TokenType.THEN,
    'elif': TokenType.ELIF,
    'else': TokenType.ELSE,
    'end': TokenType.END,
    'while': TokenType.WHILE,
    'for': TokenType.FOR,
    'in': TokenType.IN,
    'do': TokenType.DO,
    'and': TokenType.AND,
    'or': TokenType.OR,
    'not': TokenType.NOT,
    'true': TokenType.TRUE,
    'false': TokenType.FALSE,
}

@dataclass(slots=True)
class Token:
    type: TokenType
    value: str | int | float
    line: int
    col: int
    
    def __repr__(self):
        return f"Token({self.type.name}, {self.value!r}, {self.line}:{self.col})"

class Lexer:
    """High-performance lexer with single-pass tokenization"""
    
    __slots__ = ('source', 'pos', 'line', 'col', 'length')
    
    def __init__(self, source: str):
        self.source = source
        self.pos = 0
        self.line = 1
        self.col = 1
        self.length = len(source)
    
    def peek(self, offset: int = 0) -> str:
        """Look ahead without consuming"""
        idx = self.pos + offset
        return self.source[idx] if idx < self.length else '\0'
    
    def advance(self) -> str:
        """Consume and return current character"""
        ch = self.peek()
        self.pos += 1
        if ch == '\n':
            self.line += 1
            self.col = 1
        else:
            self.col += 1
        return ch
    
    def skip_whitespace(self):
        """Skip spaces and tabs (not newlines - they're significant)"""
        while self.peek() in ' \t\r':
            self.advance()
    
    def skip_comment(self):
        """Skip single-line and multi-line comments"""
        if self.peek() == '/' and self.peek(1) == '/':
            while self.peek() not in ('\n', '\0'):
                self.advance()
        elif self.peek() == '/' and self.peek(1) == '*':
            self.advance()  # /
            self.advance()  # *
            while not (self.peek() == '*' and self.peek(1) == '/'):
                if self.peek() == '\0':
                    return  # Unterminated comment
                self.advance()
            self.advance()  # *
            self.advance()  # /
    
    def make_token(self, type: TokenType, value, line: int, col: int) -> Token:
        return Token(type, value, line, col)
    
    def read_string(self) -> Token:
        """Parse string literal"""
        line, col = self.line, self.col
        quote = self.advance()  # Opening quote
        chars = []
        
        while self.peek() != quote and self.peek() != '\0':
            ch = self.advance()
            if ch == '\\':
                escape = self.advance()
                chars.append({
                    'n': '\n', 't': '\t', 'r': '\r',
                    '\\': '\\', '"': '"', "'": "'"
                }.get(escape, escape))
            else:
                chars.append(ch)
        
        if self.peek() == '\0':
            return self.make_token(TokenType.ERROR, "Unterminated string", line, col)
        
        self.advance()  # Closing quote
        return self.make_token(TokenType.STRING, ''.join(chars), line, col)
    
    def read_number(self) -> Token:
        """Parse integer or float literal"""
        line, col = self.line, self.col
        start = self.pos
        is_float = False
        
        # Handle hex/binary/octal
        if self.peek() == '0' and self.peek(1) in 'xXbBoO':
            self.advance()  # 0
            prefix = self.advance().lower()
            
            if prefix == 'x':
                while self.peek() in '0123456789abcdefABCDEF_':
                    self.advance()
                return self.make_token(TokenType.INT, int(self.source[start:self.pos].replace('_', ''), 16), line, col)
            elif prefix == 'b':
                while self.peek() in '01_':
                    self.advance()
                return self.make_token(TokenType.INT, int(self.source[start:self.pos].replace('_', ''), 2), line, col)
            elif prefix == 'o':
                while self.peek() in '01234567_':
                    self.advance()
                return self.make_token(TokenType.INT, int(self.source[start:self.pos].replace('_', ''), 8), line, col)
        
        # Decimal
        while self.peek().isdigit() or self.peek() == '_':
            self.advance()
        
        # Float part
        if self.peek() == '.' and self.peek(1).isdigit():
            is_float = True
            self.advance()  # .
            while self.peek().isdigit() or self.peek() == '_':
                self.advance()
        
        # Exponent
        if self.peek() in 'eE':
            is_float = True
            self.advance()
            if self.peek() in '+-':
                self.advance()
            while self.peek().isdigit():
                self.advance()
        
        num_str = self.source[start:self.pos].replace('_', '')
        if is_float:
            return self.make_token(TokenType.FLOAT, float(num_str), line, col)
        return self.make_token(TokenType.INT, int(num_str), line, col)
    
    def read_identifier(self) -> Token:
        """Parse identifier or keyword"""
        line, col = self.line, self.col
        start = self.pos
        
        while self.peek().isalnum() or self.peek() == '_':
            self.advance()
        
        text = self.source[start:self.pos]
        token_type = KEYWORDS.get(text, TokenType.IDENT)
        return self.make_token(token_type, text, line, col)
    
    def next_token(self) -> Token:
        """Get next token"""
        self.skip_whitespace()
        
        # Skip comments
        while self.peek() == '/' and self.peek(1) in '/*':
            self.skip_comment()
            self.skip_whitespace()
        
        line, col = self.line, self.col
        ch = self.peek()
        
        if ch == '\0':
            return self.make_token(TokenType.EOF, '', line, col)
        
        if ch == '\n':
            self.advance()
            return self.make_token(TokenType.NEWLINE, '\n', line, col)
        
        # String
        if ch in '"\'':
            return self.read_string()
        
        # Number
        if ch.isdigit():
            return self.read_number()
        
        # Identifier/keyword
        if ch.isalpha() or ch == '_':
            return self.read_identifier()
        
        # Two-character operators
        ch2 = self.peek(1)
        two_char = ch + ch2
        
        TWO_CHAR_OPS = {
            '==': TokenType.EQ,
            '!=': TokenType.NEQ,
            '<=': TokenType.LTE,
            '>=': TokenType.GTE,
            '->': TokenType.ARROW,
            '..': TokenType.RANGE,
            '<<': TokenType.SHL,
            '>>': TokenType.SHR,
        }
        
        if two_char in TWO_CHAR_OPS:
            self.advance()
            self.advance()
            return self.make_token(TWO_CHAR_OPS[two_char], two_char, line, col)
        
        # Single-character operators
        SINGLE_CHAR_OPS = {
            '+': TokenType.PLUS,
            '-': TokenType.MINUS,
            '*': TokenType.STAR,
            '/': TokenType.SLASH,
            '%': TokenType.PERCENT,
            '<': TokenType.LT,
            '>': TokenType.GT,
            '=': TokenType.ASSIGN,
            '&': TokenType.BAND,
            '|': TokenType.BOR,
            '^': TokenType.BXOR,
            '(': TokenType.LPAREN,
            ')': TokenType.RPAREN,
            '[': TokenType.LBRACKET,
            ']': TokenType.RBRACKET,
            ',': TokenType.COMMA,
            ':': TokenType.COLON,
        }
        
        if ch in SINGLE_CHAR_OPS:
            self.advance()
            return self.make_token(SINGLE_CHAR_OPS[ch], ch, line, col)
        
        # Unknown character
        self.advance()
        return self.make_token(TokenType.ERROR, f"Unexpected character: {ch}", line, col)
    
    def tokenize(self) -> List[Token]:
        """Tokenize entire source into list"""
        tokens = []
        while True:
            token = self.next_token()
            tokens.append(token)
            if token.type == TokenType.EOF:
                break
        return tokens


def tokenize(source: str) -> List[Token]:
    """Convenience function to tokenize source code"""
    return Lexer(source).tokenize()
