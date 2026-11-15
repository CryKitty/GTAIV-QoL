# Bundled runtime notices

Tcl-license.terms and Tk-license.terms are upstream notices from:

- https://github.com/tcltk/tcl/blob/core-8-6-14/license.terms
- https://github.com/tcltk/tk/blob/core-8-6-14/license.terms

The bytes match their respective core-8-6-13 notices. The supported CPython 3.12
Windows runtime bundles Tcl/Tk 8.6; its installer does not include these upstream
notice files. The build copies these complete notices plus the installed Python
and PyInstaller notices into WINDOWS-RUNTIME-LICENSES.txt in the Windows ZIP.
The builder rejects a different Tcl/Tk major/minor pending a licensing review.
No Tcl/Tk source/runtime or new player dependency is downloaded here.
