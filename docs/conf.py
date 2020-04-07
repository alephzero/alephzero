###########################################################################
#          General Info                                                   #
###########################################################################

project = 'AlephZero'
copyright = '2020, Leonid Shamis'
author = 'Leonid Shamis'

###########################################################################
#          Extensions                                                     #
###########################################################################

extensions = []

extensions += ['breathe']
breathe_projects = {'AlephZero': './doxygen_build/xml/'}
breathe_default_project = 'AlephZero'

###########################################################################
#          Scripts                                                        #
###########################################################################

import os
import subprocess
import sys

def run_doxygen(app):
    try:
        subprocess.check_call(['doxygen'])
    except subprocess.CalledProcessError as err:
        print('doxygen failed:', err, file=sys.stderr)


def setup(app):
    app.connect('builder-inited', run_doxygen)
