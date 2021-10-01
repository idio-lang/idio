# Copyright (c) 2021 Ian Fitchet <idf@idio-lang.org>
#
# Licensed under the Apache License, Version 2.0 (the "License"); you
# may not use this file except in compliance with the License.  You
# may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#

# 
# idio_domain.py -- an Idio domain for Sphinx
#

# With nods to Russel Sim's cldomain.py,
# https://github.com/russell/sphinxcontrib-cldomain, and the Met
# Office's rose_domain.py,
# https://github.com/metomi/rose/blob/2018.05.0/sphinx/ext/rose_domain.py
# and the standard offerings in .../sphinx/domains/python.py etc.

import re
import sys
from typing import cast

from docutils import nodes
from docutils.nodes import block_quote
from docutils.parsers.rst import directives

from sphinx import addnodes
from sphinx.directives import ObjectDescription
from sphinx.domains import Domain, ObjType, Index, IndexEntry
from sphinx.roles import XRefRole
from sphinx.util import logging
from sphinx.util.docfields import Field, TypedField, GroupedField
from sphinx.util.docutils import SphinxDirective
from sphinx.util.nodes import make_id, make_refnode

class desc_idio_parameterlist(addnodes.desc_parameterlist):
    """Node for an Idio parameter list."""
    child_text_separator = ' '

def visit_html_idio_parameterlist(self, node):
    self.first_param = True
    self.body.append(' ')
    self.param_separator = node.child_text_separator

def depart_html_idio_parameterlist(self, node):
    pass

def visit_text_idio_parameterlist(self, node):
    self.first_param = True
    self.add_text(' ')
    self.param_separator = node.child_text_separator

def depart_text_idio_parameterlist(self, node):
    pass

class desc_idio_parameter(addnodes.desc_parameter):
    """Node for an Idio parameter item."""

def depart_text_idio_parameter(self, node):
    pass

def visit_html_idio_parameter(self, node):
    if not self.first_param:
        self.body.append(self.param_separator)
    else:
        self.first_param = False
    if not node.hasattr('noemph'):
        self.body.append('<em>')


def depart_html_idio_parameter(self, node):
    if not node.hasattr('noemph'):
        self.body.append('</em>')


def visit_text_idio_parameter(self, node):
    if not self.first_param:
        self.add_text(self.param_separator)
    else:
        self.first_param = False
    self.add_text(node.astext())
    raise nodes.SkipNode


def visit_bs_html_desc_type(self, node):
    self.body.append(self.param_separator)
    self.body.append(self.starttag(node, 'tt', '', CLASS='desc-type'))


def depart_bs_html_desc_type(self, node):
    self.body.append('</tt>')


def visit_html_desc_type(self, node):
    self.body.append(self.param_separator)

class IdioModuleDirective(SphinxDirective):
    """
    Directive to mark description of a new module.
    """

    has_content = False
    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = False
    option_spec = {
        'noindex': directives.flag,
    }

    def run(self):
        domain = cast(IdioDomain, self.env.get_domain('idio'))

        modname = self.arguments[0].strip()
        noindex = 'noindex' in self.options
        self.env.ref_context['idio:module'] = modname
        ret = []  # type: List[Node]
        if not noindex:
            # note module to the domain
            node_id = make_id(self.env, self.state.document, 'module', modname)
            target = nodes.target('', '', ids=[node_id], ismod=True)
            self.set_source_info(target)

            self.state.document.note_explicit_target(target)

            domain.note_module(modname, node_id)
            domain.note_symbol(modname, 'module', node_id, location=target)

            ret.append(target)
            indextext = '%s; %s' % ('module', modname)
            inode = addnodes.index(entries=[('pair', indextext, node_id, '', None)])
            ret.append(inode)
        return ret

class IdioCurrentModule(SphinxDirective):
    """
    This directive is just to tell Sphinx that we're documenting
    stuff in module foo, but links to module foo won't lead here.
    """

    has_content = False
    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = False
    option_spec = {}  # type: Dict

    def run(self):
        modname = self.arguments[0].strip()
        if modname == 'None':
            self.env.ref_context.pop('idio:module', None)
        else:
            self.env.ref_context['idio:module'] = modname
        return []


class IdioDirective(ObjectDescription):
    """Base class for other Idio Directives

    Subclasses must provide

    NAME
    LABEL

    Subclasses may provide

    doc_field_types
    """

    NAME = None
    LABEL = ''

    def __init__(self, *args, **kwargs):
        self.ref_context_to_remove = []  # Cannot do this in run().
        self.registered_children = []
        ObjectDescription.__init__(self, *args, **kwargs)

    def run(self):
        result = super(IdioDirective, self).run()
        return result

    def get_signature_prefix(self, sig):
        return self.objtype + ' '

    def handle_signature(self, sig, signode):
        #fullname, prefix = super().handle_signature(sig, signode)
        #return fullname, prefix

        objtype = self.get_signature_prefix(sig)
        signode += addnodes.desc_annotation(objtype, objtype)

        sig_split = sig.split(" ")
        sig_name = sig_split[0]
        if len(sig_split) > 1:
            sig_args = sig_split[1:]
        else:
            sig_args = None

        m = re.match ("(.*/)([^/]+)", sig_name)
        if m:
            sig_module = m.group (1)
            sig_name = m.group (2)
        else:
            sig_module = None

        if sig_module:
            signode += addnodes.desc_addname(sig_module, sig_module)

        signode += addnodes.desc_name(sig_name, sig_name)

        if sig_args:
            params = desc_idio_parameterlist()
            for arg in sig_args:
                params += desc_idio_parameter(arg, arg)
            signode += params

        symbol_name = sig_name
        if not symbol_name:
            raise Exception("Unknown symbol type for signature %s" % sig_name)

        return objtype.strip(), symbol_name

    def get_index_text(self, modname, name_cls):
        objtype, name = name_cls
        if modname:
            return '%s (%s in module %s)' % (name, objtype, modname)
        else:
            return '%s (%s)' % (name, objtype)

    def add_target_and_index(self, name_cls, sig, signode):
        objtype, name = name_cls
        modname = self.options.get('module', self.env.ref_context.get('idio:module'))
        fullname = (modname + '/' if modname else '') + name

        node_id = make_id(self.env, self.state.document, '', fullname)
        signode['ids'].append(node_id)

        self.state.document.note_explicit_target(signode)

        domain = cast(IdioDomain, self.env.get_domain('idio'))
        domain.note_symbol(fullname, self.objtype, node_id, location=signode)

        if 'noindexentry' not in self.options:
            indextext = self.get_index_text(modname, name_cls)
            if indextext:
                self.indexnode['entries'].append(('single', indextext, node_id, '', None))

class IdioTypeDirective(IdioDirective):
    NAME = 'type'
    LABEL = 'Idio type'

class IdioFunctionDirective(IdioDirective):
    NAME = 'function'
    LABEL = 'Idio function'

class IdioTemplateDirective(IdioDirective):
    NAME = 'template'
    LABEL = 'Idio template'

class IdioValueDirective(IdioDirective):
    NAME = 'value'
    LABEL = 'Idio value'

class IdioConditionDirective(IdioDirective):
    NAME = 'condition'
    LABEL = 'Idio condition'

class IdioStructDirective(IdioDirective):
    NAME = 'struct'
    LABEL = 'Idio struct'

class IdioXRefRole(XRefRole):
    """Handle references to Idio objects."""

    def process_link(self, env, refnode, has_explicit_title, title, target):
        if not has_explicit_title:
            pass
        return title, target

class IdioModuleIndex(Index):
    """
    Index subclass to provide the Idio module index.
    """

    name = 'modindex'
    localname = 'Idio Module Index'
    shortname = 'modules'

    def generate(self, docnames=None):
        content = {}  # type: Dict[str, List[IndexEntry]]
        # list of prefixes to ignore
        ignores = None  # type: List[str]
        ignores = self.domain.env.config['modindex_common_prefix']  # type: ignore
        ignores = sorted(ignores, key=len, reverse=True)
        # list of all modules, sorted by module name
        modules = sorted(self.domain.data['modules'].items(),
                         key=lambda x: x[0].lower())
        # sort out collapsable modules
        prev_modname = ''
        num_toplevels = 0
        for modname, (docname, node_id) in modules:
            if docnames and docname not in docnames:
                continue

            for ignore in ignores:
                if modname.startswith(ignore):
                    modname = modname[len(ignore):]
                    stripped = ignore
                    break
            else:
                stripped = ''

            # we stripped the whole module name?
            if not modname:
                modname, stripped = stripped, ''

            entries = content.setdefault(modname[0].lower(), [])

            package = modname.split('.')[0]
            if package != modname:
                # it's a submodule
                if prev_modname == package:
                    # first submodule - make parent a group head
                    if entries:
                        last = entries[-1]
                        entries[-1] = IndexEntry(last[0], 1, last[2], last[3],
                                                 last[4], last[5], last[6])
                elif not prev_modname.startswith(package):
                    # submodule without parent in list, add dummy entry
                    entries.append(IndexEntry(stripped + package, 1, '', '', '', '', ''))
                subtype = 2
            else:
                num_toplevels += 1
                subtype = 0

            entries.append(IndexEntry(stripped + modname, subtype, docname, node_id, '', '', ''))
            prev_modname = modname

        # apply heuristics when to collapse modindex at page load:
        # only collapse if number of toplevel modules is larger than
        # number of submodules
        collapse = len(modules) - num_toplevels < num_toplevels

        # sort by first letter
        sorted_content = sorted(content.items())

        return sorted_content, collapse

class IdioDomain(Domain):
    """Idio language Domain"""

    # prefix for Idio domain
    name = 'idio'
    # label for Idio domain
    label = 'Idio'

    # object_types and directives should match

    # 'value' represents variables that might change and constants
    # (that shouldn't)
    object_types = {
        'type':      ObjType ('type', 'type', 'obj'),
        'module':    ObjType ('module', 'module', 'obj'),
        'function':  ObjType ('function', 'function', 'obj'),
        'template':  ObjType ('template', 'template', 'obj'),
        'value':     ObjType ('value', 'value', 'obj'),
        'condition': ObjType ('condition', 'condition', 'obj'),
        'struct': ObjType ('struct', 'struct', 'obj'),
                    }

    # object_types and directives should match
    directives = {
        'type':          IdioTypeDirective,
        'module':        IdioModuleDirective,
        'currentmodule': IdioCurrentModule,
        'function':      IdioFunctionDirective,
        'template':      IdioTemplateDirective,
        'value':         IdioValueDirective,
        'condition':     IdioConditionDirective,        
        'struct':        IdioStructDirective,        
    }

    # a text role per item in object_types
    roles = {
        'type':      IdioXRefRole,
        'module':    IdioXRefRole,
        'function':  IdioXRefRole,
        'template':  IdioXRefRole,
        'value':     IdioXRefRole,
        'condition': IdioXRefRole,        
        'struct':    IdioXRefRole,        
    }

    # initial value of self.data
    initial_data = {
        'symbols': {},
        'modules': {},
    }

    indices = [
        IdioModuleIndex,
    ]

    # clear_doc is required
    def clear_doc(self, docname):
        for fullname, (pkg_docname, _id, _ot) in list(self.data['symbols'].items()):
            if pkg_docname == docname:
                del self.data['symbols'][fullname]
        for modname, mod_docname in list(self.data['modules'].items()):
            if mod_docname == docname:
                del self.data['modules'][modname]

    # this is get_X where X is a key in initial_data
    def get_symbols(self):
        for refname, (docname, type_) in list(self.data['symbols'].items()):
            yield refname, refname, type_, docname, refname, 1

    # a helper function for resolve_xref
    def find_symbol(self, env, name):
        def filter_symbols(symbol):
            if name == symbol[0]:
                return True
            return False

        return filter(filter_symbols, symbols.items())

    # resolve_xref is required
    def resolve_xref(self, env, fromdocname, builder, typ, target, node, contnode):
        """Associate a reference with a documented object.

        :param typ: the object type - see object_types
        :type typ: str
        :param target: the rest of the reference as written in the rst
        :type target: string

        """

        matches = self.find_symbol(env, target)

        if not matches:
            return None
        elif len(matches) > 1:
            env.warn_node(
                'more than one target found for cross-reference '
                '%r: %s' % (target, ', '.join(match[0] for match in matches)),
                node)

        name = matches[0][0]    # symbol name
        filename = matches[0][1][0][0] # first filename
        type = matches[0][1][0][1]     # first type
        link = type + ":" + name

        return make_refnode (builder, fromdocname, filename, link, contnode, name)

    def note_module(self, name, node_id):
        """Note an Idio module for cross reference."""
        self.data['modules'][name] = (self.env.docname, node_id)

    def note_symbol(self, name, objtype, node_id, location=None):
        """Note an Idio symbol for cross reference."""
        if name in self.data['symbols']:
            other = self.data['symbols'][name]
            print ("duplicate object description of {0}, other instance in {1}, use :noindex: for one of them".format (name, other), file=sys.stderr)
        self.data['symbols'][name] = (self.env.docname, node_id, objtype)

def setup(app):
    app.add_domain(IdioDomain)
    app.add_node(desc_idio_parameterlist,
                 html=(visit_html_idio_parameterlist, depart_html_idio_parameterlist),
                 text=(visit_text_idio_parameterlist, depart_text_idio_parameterlist))
    app.add_node(desc_idio_parameter,
                 html=(visit_html_idio_parameter, depart_html_idio_parameter),
                 text=(visit_text_idio_parameter, depart_text_idio_parameter))
