from docutils import nodes
from docutils.parsers.rst import Directive

from sphinx.locale import _
from sphinx.util.docutils import SphinxDirective

# the git-oriented features are derived from Daniel Watkins'
# sphinx-git code

from datetime import datetime, timezone
from git import Repo

##############################

class aside(nodes.General, nodes.Element):
    pass

def visit_aside_html(self, node):
    self.body.append (self.starttag (node, 'aside', CLASS='aside'))

def depart_aside_html(self, node):
    self.body.append ('</aside>')

def visit_aside_text(self, node):
    self.new_state()
    self.add_text ('aside:')

def depart_aside_text(self, node):
    self.end_state()

class AsideDirective(SphinxDirective):

    has_content = True

    def run(self):
        targetid = 'aside-%d' % self.env.new_serialno('aside')
        targetnode = nodes.target('', '', ids=[targetid])

        aside_node = aside('\n'.join(self.content))
        self.state.nested_parse(self.content, self.content_offset, aside_node)

        return [targetnode, aside_node]

##############################

class sidebox(nodes.General, nodes.Element):
    pass

def visit_sidebox_html(self, node):
    self.body.append (self.starttag (node, 'div', CLASS='sidebox'))

def depart_sidebox_html(self, node):
    self.body.append ('</div>')

def visit_sidebox_text(self, node):
    self.new_state()
    self.add_text ('sidebox:')

def depart_sidebox_text(self, node):
    self.end_state()
    pass

class SideboxDirective(SphinxDirective):

    has_content = True

    def run(self):
        targetid = 'sidebox-%d' % self.env.new_serialno('sidebox')
        targetnode = nodes.target('', '', ids=[targetid])

        sidebox_node = sidebox('\n'.join(self.content))
        self.state.nested_parse(self.content, self.content_offset, sidebox_node)

        return [targetnode, sidebox_node]

##############################

class gitcommit (nodes.General, nodes.Element):
    pass

def visit_gitcommit_html(self, node):
    self.body.append (self.starttag (node, 'div', CLASS='gitcommit'))

def depart_gitcommit_html(self, node):
    self.body.append ('</div>')

def visit_gitcommit_text(self, node):
    self.new_state()
    self.add_text ('git commit')

def depart_gitcommit_text(self, node):
    self.end_state()
    pass

# we want a git-prompt -like concoction, "commit(master) *"
class GitCommitDirective (SphinxDirective):
    default_sha_length = 7

    has_content = True

    option_spec = {
        'branch': bool,
        'commit': bool,
        'uncommitted': bool,
        'sha_length': int,
    }

    def run (self):
        self.repo = self._find_repo ()
        self.branch_name = None
        if not self.repo.head.is_detached:
            self.branch_name = self.repo.head.ref.name
        self.commit = self.repo.commit ()
        self.sha_length = self.options.get ('sha_length',
                                            self.default_sha_length)

        now = datetime.now(timezone.utc)
        text = "Last built at {0} from ".format (now.strftime ("%Y-%m-%dT%H:%M:%SZ%z"))
        if 'commit' in self.options:
            text += "{0}".format (self.commit.hexsha[:self.sha_length])
        if 'branch' in self.options and self.branch_name is not None:
            text += " ({0})".format (self.branch_name)
        if 'uncommitted' in self.options and self.repo.is_dirty ():
            text += " *"
        self.text = text
        markup = gitcommit ()
        markup.append (nodes.paragraph (text=text))

        return [markup]

    def _find_repo (self):
        env = self.state.document.settings.env
        repo = Repo(env.srcdir, search_parent_directories=True)
        return repo


def setup(app):
    app.add_directive("aside", AsideDirective)
    app.add_directive("sidebox", SideboxDirective)
    app.add_directive("gitcommit", GitCommitDirective)

    app.add_node(aside,
                 html=(visit_aside_html, depart_aside_html),
                 text=(visit_aside_text, depart_aside_text))

    app.add_node(sidebox,
                 html=(visit_sidebox_html, depart_sidebox_html),
                 text=(visit_sidebox_text, depart_sidebox_text))

    app.add_node(gitcommit,
                 html=(visit_gitcommit_html, depart_gitcommit_html),
                 text=(visit_gitcommit_text, depart_gitcommit_text))

    return {
        'version': '0.1',
        'parallel_read_safe': True,
        'parallel_write_safe': True,
    }
