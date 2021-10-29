# Configuration file for the Sphinx documentation builder.
#
# This file only contains a selection of the most common options. For a full
# list see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Path setup --------------------------------------------------------------

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
#
import os
import sys
import datetime
# sys.path.insert(0, os.path.abspath('.'))
sys.path.append(os.path.abspath('./_ext'))

# -- Project information -----------------------------------------------------

project = 'Idio'
now = datetime.datetime.now()
copyright = '{0}, Ian Fitchet'.format (now.year)
author = 'Ian Fitchet'

# The full version, including alpha/beta/rc tags
release = '0.0'


# -- General configuration ---------------------------------------------------

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = [
    'sphinx.ext.graphviz',
    'sphinx.ext.intersphinx',
    'sphinx_git',
    'aside',
    'idio_lexer',
    'idio_domain',
]

# Add any paths that contain templates here, relative to this directory.
templates_path = ['_templates']

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
# This pattern also affects html_static_path and html_extra_path.
exclude_patterns = []

intersphinx_mapping = {
    'ref': ('https://idio-lang.org/docs/ref', None),
}

import idio_lexer

# -- Options for HTML output -------------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
#
html_theme = 'idio-theme'

html_theme_path = ['.']

html_short_title = 'Idio'

# Alabaster theme options
html_theme_options = {
    'code_font_family': "'Source Code Pro', 'Consolas', 'Menlo', 'DejaVu Sans Mono', 'Bitstream Vera Sans Mono', monospace",
    'font_family': "'Open Sans', 'Verdana', serif",
    'body_text_align': "justify",

    'description': 'the programmable shell',

    # Watch button URL will be
    # https://github.com/{github_user}/{github_repo}
    'github_button': True,
    'github_user': 'idio-lang',
    'github_repo': 'sphinx-source',

    # Normally these would be used in the sidebar navigation.html
    # (which we comment out below) but we re-use in the header
    'extra_nav_links': {
        'Home': '/',
        'Guide': '/docs/guide/',
        'Reference': '/docs/ref/',
        'DIPS': '/docs/DIPS/',
    },
}

html_sidebars = {
    '**': [
        'about.html',
        # 'navigation.html',
        'localtoc.html',
        'relations.html',
        'searchbox.html',
        'donate.html',
        # 'sourcelink.html',
    ]
}

# affects creation of last_updated used in the footer block in
# idio-theme/_templates/layout.html
html_last_updated_fmt = '%Y%m%d-%H%M%S'

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['_static']
