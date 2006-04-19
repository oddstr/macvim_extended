" Vim indent file
" Language:         YACC input file
" Maintainer:       Nikolai Weibull <nikolai+work.vim@bitwi.se>
" Latest Revision:  2005-06-29

" Only load this indent file when no other was loaded.
if exists("b:did_indent")
  finish
endif

let b:did_indent = 1

setlocal indentexpr=GetYaccIndent()
setlocal indentkeys=!^F,o,O

" Only define the function once.
if exists("*GetYaccIndent")
  finish
endif

function GetYaccIndent()
  if v:lnum == 1
    return 0
  endif

  let ind = indent(v:lnum - 1)
  let line = getline(v:lnum - 1)

  if line == ''
    let ind = 0
  elseif line =~ '^\w\+\s*:'
    let ind = ind + matchend(line, '^\w\+\s*')
  elseif line =~ '^\s*;'
    let ind = 0
  else
    let ind = indent(v:lnum)
  endif

  return ind
endfunction