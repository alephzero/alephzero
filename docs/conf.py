project = 'AlephZero'
copyright = '2020, Leonid Shamis'
author = 'Leonid Shamis'

extensions = []

extensions += ['breathe']
breathe_projects = {'AlephZero': './doxygen_build/xml/'}
breathe_default_project = 'AlephZero'

templates_path = ['_templates']

exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']

extensions += ['sphinx_rtd_theme']
html_theme = 'sphinx_rtd_theme'

html_static_path = ['_static']

import subprocess
import sys

def run_doxygen(app):
    try:
        subprocess.check_call(['make', 'doxygen'])
    except subprocess.CalledProcessError as err:
        sys.stderr.write("doxygen execution failed: %s" % err)


def setup(app):
    app.connect('builder-inited', run_doxygen)
