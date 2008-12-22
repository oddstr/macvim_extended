
                    Føroyska orðalistan til rættlestur (0.2)

The Faroese Dictionary for Spell Checking from FLUG. This series of releases
contains all words from our word list, that have been verified as correct by
at least one reviewer.

You can read more about the project at:

   http://fo.speling.org/

Download the latest edition of this series from:

   http://fo.speling.org/filer/

----------------------------------------------------------------------------

Um har er feilur í orðalistanum ber til at boða frá á:

                   http://fo.speling.org/fejlmelding/

----------------------------------------------------------------------------

Installation instructions (by Flemming Hjernoe <glfhj@post2.tele.dk>, modified
by Jacob Sparre Andersen <sparre@nbi.dk>):


==============================================================================
OpenOffice.org1.0.1:
--------------------

Run one of the commands:

   make install

or

   make user_install

Due to an apparent lack of foresight from the OOo developers you have to
choose Greek, when you mean Faroese.

Alternatively you will have to copy the files to
".../OpenOffice.org1.0.1/share/dict/ooo/" yourself and then find the file
"dictionary.lst", where you change the line:

DICT el GR el_GR

to

DICT el GT fo_FO

==============================================================================
To install dictionaries in OpenOffice build OO641C to 1.0:
----------------------------------------------------------

Run one of the commands:

   make install

or

   make user_install

Due to an apparent lack of foresight from the OOo developers you have to
choose Greek, when you mean Faroese.

Unzip the dictionary files in the /OpenOffice.orgxxx/user/wordbook/ directory.

Edit the dictionary.lst file that is in that same directory using any text
editor to register a dictionary for a specific locale (the same dictionary can
be registered for multiple locales).

   tar xzf myspell-fo-0.2.1.tar.gz
   cd myspell-fo-0.2.1
   mv fo_FO.{aff,dic} /OpenOffice.orgxxx/user/wordbook
   echo 'DICT el GR fo_FO' >> /OpenOffice.orgxxx/user/wordbook/dictionary.lst

Start up OpenOffice and go to:

   Tools->Options->LanguageSettings->WritingAids
   Hit "Edit" (the upper one) and use the pull down menu to select your locale=Faroese
   and then make sure to check the MySpell SpellChecker for that locale. 
   Go to Tools->Options->LanguageSettings->Languages
   Change the settings to "Faroese" See note!

Your dictionary is installed and registered as Greek (OOo will not recognise
the existence of the Faroese language).

fotr: The Faroese Dictionary for Spell Checking
© 2000-2004 The Faroese Linux User Group (www.flug.fo) and the contributors.

    This dictionary is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This dictionary is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this dictionary; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
