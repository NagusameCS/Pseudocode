" Vim indent file for Pseudocode
" Language: Pseudocode
" Maintainer: NagusameCS
" License: MIT

if exists("b:did_indent")
  finish
endif
let b:did_indent = 1

setlocal indentexpr=GetPseudocodeIndent()
setlocal indentkeys+=0=end,0=else,0=elif,0=case

if exists("*GetPseudocodeIndent")
  finish
endif

function! GetPseudocodeIndent()
  let lnum = prevnonblank(v:lnum - 1)
  if lnum == 0
    return 0
  endif

  let prevline = getline(lnum)
  let curline = getline(v:lnum)
  let ind = indent(lnum)

  " Increase indent after block openers
  if prevline =~ '^\s*\(fn\|if\|elif\|else\|while\|for\|match\|case\)\b'
    let ind += shiftwidth()
  endif

  " Decrease indent for block closers
  if curline =~ '^\s*\(end\|else\|elif\|case\)\b'
    let ind -= shiftwidth()
  endif

  return ind
endfunction
