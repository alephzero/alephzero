project = 'AlephZero'
copyright = '2020, Leonid Shamis'
author = 'Leonid Shamis'

extensions = []

extensions += ['breathe', 'exhale']
breathe_projects = {'AlephZero': './doxygen_build/xml/'}
breathe_default_project = 'AlephZero'

exhale_args = {
    "containmentFolder":     "./api",
    "rootFileName":          "library_root.rst",
    "rootFileTitle":         "Library API",
    "doxygenStripFromPath":  "../../alephzero",
    "createTreeView":        False,
}


templates_path = ['_templates']

exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']

extensions += ['sphinx_rtd_theme']
html_theme = 'sphinx_rtd_theme'

html_static_path = ['_static']
