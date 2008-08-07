"------------------------------------------------------------------------------
"  Description: Vim Ada/GNAT compiler file
"     Language: Ada (GNAT)
"          $Id: gnat.vim,v 1.8 2008/08/06 16:56:26 vimboss Exp $
"    Copyright: Copyright (C) 2006 Martin Krischik
"   Maintainer:	Martin Krischi <krischik@users.sourceforge.net>k
"		Ned Okie <nokie@radford.edu>
"      $Author: vimboss $
"        $Date: 2008/08/06 16:56:26 $
"      Version: 4.6
"    $Revision: 1.8 $
"     $HeadURL: https://gnuada.svn.sourceforge.net/svnroot/gnuada/trunk/tools/vim/compiler/gnat.vim $
"      History: 24.05.2006 MK Unified Headers
"		16.07.2006 MK Ada-Mode as vim-ball
"               15.10.2006 MK Bram's suggestion for runtime integration
"		19.09.2007 NO use project file only when there is a project
"    Help Page: compiler-gnat
"------------------------------------------------------------------------------

if (exists("current_compiler")	    &&
   \ current_compiler == "gnat")    ||
   \ version < 700
   finish
endif

let current_compiler = "gnat"

if !exists("g:gnat")
   let g:gnat = gnat#New ()

   call ada#Map_Menu (
      \ 'GNAT.Build',
      \ '<F7>',
      \ 'call gnat.Make ()')
   call ada#Map_Menu (
      \ 'GNAT.Pretty Print',
      \ ':GnatPretty',
      \ 'call gnat.Pretty ()')
   call ada#Map_Menu (
      \ 'GNAT.Tags',
      \ ':GnatTags',
      \ 'call gnat.Tags ()')
   call ada#Map_Menu (
      \ 'GNAT.Find',
      \ ':GnatFind',
      \ 'call gnat.Find ()')
   call ada#Map_Menu (
      \ 'GNAT.Set Projectfile\.\.\.',
      \ ':SetProject',
      \ 'call gnat.Set_Project_File ()')

   call g:gnat.Set_Session ()
endif

if exists(":CompilerSet") != 2
   "
   " plugin loaded by other means then the "compiler" command
   "
   command -nargs=* CompilerSet setlocal <args>
endif

execute "CompilerSet makeprg="     . escape (g:gnat.Get_Command('Make'), ' ')
execute "CompilerSet errorformat=" . escape (g:gnat.Error_Format, ' ')

finish " 1}}}

"------------------------------------------------------------------------------
"   Copyright (C) 2006  Martin Krischik
"
"   Vim is Charityware - see ":help license" or uganda.txt for licence details.
"------------------------------------------------------------------------------
" vim: textwidth=0 wrap tabstop=8 shiftwidth=3 softtabstop=3 noexpandtab
" vim: foldmethod=marker
