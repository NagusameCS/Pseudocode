#!/usr/bin/env python3
"""
Pseudocode Language - Main Entry Point
Fast pseudocode interpreter with REPL and file execution
"""

import sys
import os

# Add src to path
sys.path.insert(0, os.path.dirname(__file__))

from lexer import tokenize, TokenType
from parser import parse, ParseError
from compiler import compile_program
from vm import VM, execute, RuntimeError as VMRuntimeError

VERSION = "0.1.0"

def run_file(filepath: str):
    """Execute a Pseudocode source file"""
    try:
        with open(filepath, 'r') as f:
            source = f.read()
        
        # Execute without printing result (scripts use print() explicitly)
        execute(source)
            
    except FileNotFoundError:
        print(f"Error: File not found: {filepath}", file=sys.stderr)
        sys.exit(1)
    except ParseError as e:
        print(f"Parse error: {e}", file=sys.stderr)
        sys.exit(1)
    except VMRuntimeError as e:
        print(f"Runtime error: {e}", file=sys.stderr)
        sys.exit(1)

def run_repl():
    """Interactive REPL"""
    print(f"Pseudocode {VERSION} - Type 'exit' or Ctrl+D to quit")
    
    buffer = []
    in_block = False
    
    while True:
        try:
            prompt = "... " if in_block else ">>> "
            line = input(prompt)
            
            if line.strip() == "exit":
                break
            
            buffer.append(line)
            
            # Check if we're entering a block
            if line.strip().endswith(('then', 'do')):
                in_block = True
                continue
            
            # Check if block ends
            if line.strip() == 'end':
                in_block = False
            
            if in_block:
                continue
            
            source = '\n'.join(buffer)
            buffer = []
            
            if not source.strip():
                continue
            
            try:
                result = execute(source)
                if result is not None:
                    print(result)
            except ParseError as e:
                print(f"Parse error: {e}")
            except VMRuntimeError as e:
                print(f"Runtime error: {e}")
            except Exception as e:
                print(f"Error: {e}")
                
        except EOFError:
            print()
            break
        except KeyboardInterrupt:
            print("\nInterrupted")
            buffer = []
            in_block = False

def show_help():
    print(f"""Pseudocode {VERSION} - A fast pseudocode interpreter

Usage:
  pseudocode [file.pseudo]    Run a source file
  pseudocode                  Start interactive REPL
  pseudocode --help           Show this help
  pseudocode --version        Show version

Examples:
  pseudocode hello.pseudo     Run hello.pseudo
  pseudocode                  Start REPL
""")

def main():
    args = sys.argv[1:]
    
    if not args:
        run_repl()
    elif args[0] in ('--help', '-h'):
        show_help()
    elif args[0] in ('--version', '-v'):
        print(f"Pseudocode {VERSION}")
    elif args[0].startswith('-'):
        print(f"Unknown option: {args[0]}", file=sys.stderr)
        sys.exit(1)
    else:
        run_file(args[0])

if __name__ == "__main__":
    main()
