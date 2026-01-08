"""
Pseudocode Language - Parser
Recursive descent parser with operator precedence climbing
"""

from typing import List, Optional, Callable
from lexer import Token, TokenType, Lexer
from ast_nodes import *

class ParseError(Exception):
    def __init__(self, message: str, token: Token):
        super().__init__(f"{message} at line {token.line}, col {token.col}")
        self.token = token

class Parser:
    """Recursive descent parser with Pratt-style precedence"""
    
    __slots__ = ('tokens', 'pos', 'length')
    
    # Operator precedence (higher = tighter binding)
    PRECEDENCE = {
        TokenType.OR: 1,
        TokenType.AND: 2,
        TokenType.BOR: 3,
        TokenType.BXOR: 4,
        TokenType.BAND: 5,
        TokenType.EQ: 6, TokenType.NEQ: 6,
        TokenType.LT: 7, TokenType.GT: 7, TokenType.LTE: 7, TokenType.GTE: 7,
        TokenType.SHL: 8, TokenType.SHR: 8,
        TokenType.PLUS: 9, TokenType.MINUS: 9,
        TokenType.STAR: 10, TokenType.SLASH: 10, TokenType.PERCENT: 10,
    }
    
    BINOP_MAP = {
        TokenType.PLUS: BinOp.ADD,
        TokenType.MINUS: BinOp.SUB,
        TokenType.STAR: BinOp.MUL,
        TokenType.SLASH: BinOp.DIV,
        TokenType.PERCENT: BinOp.MOD,
        TokenType.EQ: BinOp.EQ,
        TokenType.NEQ: BinOp.NEQ,
        TokenType.LT: BinOp.LT,
        TokenType.GT: BinOp.GT,
        TokenType.LTE: BinOp.LTE,
        TokenType.GTE: BinOp.GTE,
        TokenType.AND: BinOp.AND,
        TokenType.OR: BinOp.OR,
        TokenType.BAND: BinOp.BAND,
        TokenType.BOR: BinOp.BOR,
        TokenType.BXOR: BinOp.BXOR,
        TokenType.SHL: BinOp.SHL,
        TokenType.SHR: BinOp.SHR,
    }
    
    def __init__(self, tokens: List[Token]):
        self.tokens = tokens
        self.pos = 0
        self.length = len(tokens)
    
    def peek(self, offset: int = 0) -> Token:
        idx = self.pos + offset
        if idx >= self.length:
            return self.tokens[-1]  # EOF
        return self.tokens[idx]
    
    def current(self) -> Token:
        return self.peek(0)
    
    def advance(self) -> Token:
        token = self.current()
        if token.type != TokenType.EOF:
            self.pos += 1
        return token
    
    def check(self, *types: TokenType) -> bool:
        return self.current().type in types
    
    def match(self, *types: TokenType) -> Optional[Token]:
        if self.check(*types):
            return self.advance()
        return None
    
    def expect(self, type: TokenType, message: str) -> Token:
        if self.check(type):
            return self.advance()
        raise ParseError(message, self.current())
    
    def skip_newlines(self):
        while self.match(TokenType.NEWLINE):
            pass
    
    # === Expression Parsing ===
    
    def parse_expr(self, min_prec: int = 0) -> Expr:
        """Pratt parser for expressions with precedence climbing"""
        left = self.parse_unary()
        
        while True:
            token = self.current()
            prec = self.PRECEDENCE.get(token.type, -1)
            
            if prec < min_prec:
                break
            
            op_token = self.advance()
            right = self.parse_expr(prec + 1)
            
            left = BinaryExpr(
                line=op_token.line,
                col=op_token.col,
                op=self.BINOP_MAP[op_token.type],
                left=left,
                right=right
            )
        
        return left
    
    def parse_unary(self) -> Expr:
        """Parse unary operators"""
        token = self.current()
        
        if self.match(TokenType.MINUS):
            operand = self.parse_unary()
            return UnaryExpr(token.line, token.col, UnaryOp.NEG, operand)
        
        if self.match(TokenType.NOT):
            operand = self.parse_unary()
            return UnaryExpr(token.line, token.col, UnaryOp.NOT, operand)
        
        return self.parse_postfix()
    
    def parse_postfix(self) -> Expr:
        """Parse function calls and array indexing"""
        expr = self.parse_primary()
        
        while True:
            if self.match(TokenType.LPAREN):
                # Function call
                args = []
                if not self.check(TokenType.RPAREN):
                    args.append(self.parse_expr())
                    while self.match(TokenType.COMMA):
                        args.append(self.parse_expr())
                self.expect(TokenType.RPAREN, "Expected ')' after arguments")
                expr = CallExpr(expr.line, expr.col, expr, args)
            
            elif self.match(TokenType.LBRACKET):
                # Array indexing
                index = self.parse_expr()
                self.expect(TokenType.RBRACKET, "Expected ']' after index")
                expr = IndexExpr(expr.line, expr.col, expr, index)
            
            else:
                break
        
        return expr
    
    def parse_primary(self) -> Expr:
        """Parse primary expressions (literals, identifiers, parens)"""
        token = self.current()
        
        if self.match(TokenType.INT):
            return IntLiteral(token.line, token.col, token.value)
        
        if self.match(TokenType.FLOAT):
            return FloatLiteral(token.line, token.col, token.value)
        
        if self.match(TokenType.STRING):
            return StringLiteral(token.line, token.col, token.value)
        
        if self.match(TokenType.TRUE):
            return BoolLiteral(token.line, token.col, True)
        
        if self.match(TokenType.FALSE):
            return BoolLiteral(token.line, token.col, False)
        
        if self.match(TokenType.IDENT):
            return Identifier(token.line, token.col, token.value)
        
        if self.match(TokenType.LPAREN):
            expr = self.parse_expr()
            self.expect(TokenType.RPAREN, "Expected ')' after expression")
            return expr
        
        if self.match(TokenType.LBRACKET):
            # Array literal
            elements = []
            if not self.check(TokenType.RBRACKET):
                elements.append(self.parse_expr())
                while self.match(TokenType.COMMA):
                    if self.check(TokenType.RBRACKET):
                        break  # Trailing comma
                    elements.append(self.parse_expr())
            self.expect(TokenType.RBRACKET, "Expected ']' after array elements")
            return ArrayLiteral(token.line, token.col, elements)
        
        raise ParseError(f"Unexpected token: {token.type.name}", token)
    
    # === Statement Parsing ===
    
    def parse_stmt(self) -> Stmt:
        """Parse a single statement"""
        self.skip_newlines()
        
        if self.check(TokenType.LET, TokenType.CONST):
            return self.parse_var_decl()
        
        if self.check(TokenType.FN):
            return self.parse_fn_decl()
        
        if self.check(TokenType.IF):
            return self.parse_if_stmt()
        
        if self.check(TokenType.WHILE):
            return self.parse_while_stmt()
        
        if self.check(TokenType.FOR):
            return self.parse_for_stmt()
        
        if self.check(TokenType.RETURN):
            return self.parse_return_stmt()
        
        return self.parse_expr_or_assign()
    
    def parse_var_decl(self) -> VarDecl:
        """Parse variable declaration: let x = 10 or const PI = 3.14"""
        token = self.advance()
        is_const = token.type == TokenType.CONST
        
        name_token = self.expect(TokenType.IDENT, "Expected variable name")
        name = name_token.value
        
        type_ann = None
        if self.match(TokenType.COLON):
            type_token = self.expect(TokenType.IDENT, "Expected type annotation")
            type_ann = type_token.value
        
        self.expect(TokenType.ASSIGN, "Expected '=' after variable name")
        value = self.parse_expr()
        
        return VarDecl(token.line, token.col, name, type_ann, value, is_const)
    
    def parse_fn_decl(self) -> FnDecl:
        """Parse function declaration"""
        token = self.expect(TokenType.FN, "Expected 'fn'")
        name_token = self.expect(TokenType.IDENT, "Expected function name")
        
        self.expect(TokenType.LPAREN, "Expected '(' after function name")
        
        params = []
        if not self.check(TokenType.RPAREN):
            params.append(self.parse_param())
            while self.match(TokenType.COMMA):
                params.append(self.parse_param())
        
        self.expect(TokenType.RPAREN, "Expected ')' after parameters")
        
        return_type = None
        if self.match(TokenType.ARROW):
            type_token = self.expect(TokenType.IDENT, "Expected return type")
            return_type = type_token.value
        
        self.skip_newlines()
        body = self.parse_block()
        self.expect(TokenType.END, "Expected 'end' after function body")
        
        return FnDecl(token.line, token.col, name_token.value, params, return_type, body)
    
    def parse_param(self) -> Param:
        """Parse function parameter"""
        name_token = self.expect(TokenType.IDENT, "Expected parameter name")
        self.expect(TokenType.COLON, "Expected ':' after parameter name")
        type_token = self.expect(TokenType.IDENT, "Expected parameter type")
        return Param(name_token.value, type_token.value)
    
    def parse_block(self) -> List[Stmt]:
        """Parse block of statements until 'end', 'else', 'elif'"""
        statements = []
        self.skip_newlines()
        
        while not self.check(TokenType.END, TokenType.ELSE, TokenType.ELIF, TokenType.EOF):
            statements.append(self.parse_stmt())
            self.skip_newlines()
        
        return statements
    
    def parse_if_stmt(self) -> IfStmt:
        """Parse if statement"""
        token = self.expect(TokenType.IF, "Expected 'if'")
        condition = self.parse_expr()
        self.expect(TokenType.THEN, "Expected 'then' after condition")
        
        self.skip_newlines()
        then_body = self.parse_block()
        
        elif_clauses = []
        while self.match(TokenType.ELIF):
            elif_cond = self.parse_expr()
            self.expect(TokenType.THEN, "Expected 'then' after elif condition")
            self.skip_newlines()
            elif_body = self.parse_block()
            elif_clauses.append((elif_cond, elif_body))
        
        else_body = None
        if self.match(TokenType.ELSE):
            self.skip_newlines()
            else_body = self.parse_block()
        
        self.expect(TokenType.END, "Expected 'end' after if statement")
        
        return IfStmt(token.line, token.col, condition, then_body, elif_clauses, else_body)
    
    def parse_while_stmt(self) -> WhileStmt:
        """Parse while loop"""
        token = self.expect(TokenType.WHILE, "Expected 'while'")
        condition = self.parse_expr()
        self.expect(TokenType.DO, "Expected 'do' after condition")
        
        self.skip_newlines()
        body = self.parse_block()
        self.expect(TokenType.END, "Expected 'end' after while loop")
        
        return WhileStmt(token.line, token.col, condition, body)
    
    def parse_for_stmt(self) -> ForStmt:
        """Parse for loop"""
        token = self.expect(TokenType.FOR, "Expected 'for'")
        var_token = self.expect(TokenType.IDENT, "Expected loop variable")
        self.expect(TokenType.IN, "Expected 'in' after variable")
        
        start = self.parse_expr()
        
        # Check for range syntax: 0..10
        if self.match(TokenType.RANGE):
            end = self.parse_expr()
            iterable = RangeExpr(start.line, start.col, start, end)
        else:
            iterable = start
        
        self.expect(TokenType.DO, "Expected 'do' after iterable")
        
        self.skip_newlines()
        body = self.parse_block()
        self.expect(TokenType.END, "Expected 'end' after for loop")
        
        return ForStmt(token.line, token.col, var_token.value, iterable, body)
    
    def parse_return_stmt(self) -> ReturnStmt:
        """Parse return statement"""
        token = self.expect(TokenType.RETURN, "Expected 'return'")
        
        value = None
        if not self.check(TokenType.NEWLINE, TokenType.EOF, TokenType.END):
            value = self.parse_expr()
        
        return ReturnStmt(token.line, token.col, value)
    
    def parse_expr_or_assign(self) -> Stmt:
        """Parse expression statement or assignment"""
        expr = self.parse_expr()
        
        if self.match(TokenType.ASSIGN):
            value = self.parse_expr()
            return Assignment(expr.line, expr.col, expr, value)
        
        return ExprStmt(expr.line, expr.col, expr)
    
    def parse_program(self) -> Program:
        """Parse entire program"""
        statements = []
        self.skip_newlines()
        
        while not self.check(TokenType.EOF):
            statements.append(self.parse_stmt())
            self.skip_newlines()
        
        return Program(statements)


def parse(source: str) -> Program:
    """Convenience function to parse source code"""
    from lexer import tokenize
    tokens = tokenize(source)
    return Parser(tokens).parse_program()
