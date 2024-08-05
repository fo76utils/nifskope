set PATH=%PATH%;C:\Program Files\Python24

python -c "import sys, os.path; from os.path import join; sys.path = [ join(sys.prefix, 'Lib', 'site-packages', 'epydoc') ] + sys.path; script_path = os.path.abspath(sys.path[0]); sys.path = [p for p in sys.path if os.path.abspath(p) != script_path]; from epydoc.gui import gui; gui()"
