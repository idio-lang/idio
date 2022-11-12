from pygments.lexer import RegexLexer, Lexer, bygroups, include, do_insertions
from pygments.token import *

from sphinx.highlighting import lexers

import re

__all__ = [ 'IdioLexer' ]

class IdioLexer(RegexLexer):
    """
    It goes without saying that this isn't a proper Idio parser but
    satisfies, closely enough, the examples in the documentation.

    The nominal test files are source/test-lexer.idio and
    source/test-lexer.idio-console which you can test with:

    (venv) python -m pygments -x -l source/_ext/idio_lexer.py:IdioLexer source/test-lexer.idio
    (venv) python -m pygments -x -l source/_ext/idio_lexer.py:IdioConsoleLexer source/test-lexer.idio-console

    add -f html to get a clearer view on how each element is being
    parsed

    """

    # following in the style of lisp.py and shell.py

    name = 'Idio'
    aliases = [ 'idio' ]
    filenames = ['*.idio']

    special_forms = (
        'begin', 'and', 'or',
        'escape', 'quote', 'quasiquote',
        'function', 'function+',
        'if', 'cond', 'else', '=>',
        'set!',
        'define-template', 'define-infix-operator', 'define-postfix-operator',
        'define', ":=", ':*', ':~', ':$',
        'block',
        'dynamic', 'dynamic-let', 'dynamic-unset',
        'environ', 'environ-let', 'environ-unset',
        'trap', 'escape-block', 'escape-from',
        'include',
    )

    # there are several hundred builtin functions...we'll highlight
    # the ones we need (read: notice)
    builtins = (
        'eq?', 'eqv?', 'equal?',
        'symbol?', 'null?',
        'pair', 'pair?', 'ph', 'pt', 'list', 'phh', 'pht', 'pth', 'ptt', 'set-ph!', 'set-pt!',
        'printf', 'eprintf', 'display', 'display*',
        'make-array', 'array-ref', 'array-set!', 'array-push!', 'array-length',
        'make-hash', 'hash-ref', 'hash-set!',
        'define-struct',
        'define-syntax', 'syntax-rules',
        'apply', 'raise', 'call/cc', 'setter',
        'prompt', 'prompt-at', 'control', 'control-at', 'reset', 'unwind-to*',
        'for-each', 'do', 'case', 'not',
        'let', 'let*', 'letrec',
        'collect-output',
        'C/<',
        'sort',
        'gensym',
        'make-keyword-table', '%properties', '%set-properties!', 'keyword-set!',
        'trap-return', 'break', 'continue', 'while',
        'import', 'export',
        'bg',
    )

    # from standard-operators.idio and job-control.idio
    infix_operators = (
        '+', '-', '*', '/',
        'lt', 'le', 'eq', 'ge', 'gt',
        'and', 'or',
        'C/|',
        '=+', '+=', '=-', '-=',
        ':=', ':~', ':*', ':$', '=',
        '.',
        '|', '>', '<', '2>', '>&', '<&', '2>&',
        '&'
    )

    # not strictly true as it allows digits-only as a valid symbol
    # however we test this after testing for numbers so we should be
    # OK
    valid_symbol = r'[\w!%&*+,/:<=>?^~|-]+'

    tokens = {
        'root': [
            # comments
            (r';.*$', Comment.Single),
            (r'#\*', Comment.Multiline, 'multiline-comment'),
            (r'#\|', Comment.Multiline, 'multiline-sl-comment'),
            (r'#;\s*\(', Comment, 'sexp-comment'),
            # ellipses -- used a lot in examples!
            (r'\.\.\.', Comment),

            # special case for examples, 3pi/4
            (r'\dpi' + valid_symbol, Name.Variable),

            # numbers
            (r'[-+]?\d+\.\d+[dDeEfFlLsS][-+]?\d+', Number.Float),
            (r'[-+]?\d+[dDeEfFlLsS][-+]?\d+', Number.Float),
            (r'[-+]?\d+\.\d+', Number.Float),
            (r'[-+]?\d+', Number.Integer),
            (r'#b[0-1]+', Number.Bin),
            (r'#o[0-7]+', Number.Oct),
            (r'#d\d+', Number.Integer),
            (r'#x[0-9a-fA-F]+', Number.Hex),

            # this isn't strictly correct but will capture most cases
            (r'#[ei][-+]?\d\.?', Number.Float),

            # constants
            (r'(#n|#t|#f)', Name.Constant),
            (r'(#\\{[^}]+})', String.Char),
            # Pygments appears to work with #\ħ (a two byte match for . )
            (r'(#\\.)', String.Char),
            (r'(#U\+[0-9a-fA-F]+)', String.Char),

            # templates
            (r'#T{', Punctuation, 'template'),

            # pathnames
            (r'#P{', Punctuation, 'pathname-template'),

            # strings
            (r'"(\\\\|\\"|[^"])*"', String),
            (r"'" + valid_symbol, String.Symbol),
            # interpolated strings
            (r'#S.?{', Punctuation, 'interp-string'),

            # bitsets
            (r'#B{', Punctuation, 'bitset'),

            # array
            (r'#\[', Punctuation, 'array'),

            # hash
            (r'#{', Punctuation, 'hash'),

            # closures
            (r'(function\s+)(\()([^)]+)(\))(\s*{)', bygroups (Name.Function.Magic, Punctuation, Name.Variable, Punctuation, Punctuation), 'block'),
            (r'(function\s+)(' + valid_symbol + ')(\s+{)', bygroups (Name.Function.Magic, Name.Variable, Punctuation), 'block'),
            (r'(function\s+)(#n)(\s+{)', bygroups (Name.Function.Magic, Name.Constant, Punctuation), 'block'),

            # special forms
            ('(%s)' % '|'.join (re.escape (entry) + r'\s' for entry in special_forms), Name.Function.Magic),

            # common builtins
            ('(%s)' % '|'.join (re.escape (entry) + r'\s' for entry in builtins), Name.Builtin),

            # catch function names before infix_operators
            (r'(?<=\()' + valid_symbol, Name.Function),

            # infix operators
            ('(%s)' % '|'.join (re.escape (entry) + r'\s' for entry in infix_operators), Operator),

            # symbols and (Idio) keywords -- keep low down in the list
            (r':' + valid_symbol, Keyword.Pseudo),
            (r"'" + valid_symbol, String.Symbol),
            (valid_symbol, Name.Variable),

            (r'{', Punctuation, 'block'),

            (r'[()]', Punctuation),

            # whitespace is regular text -- but we try to capture some
            # in the expressions above so keep it low down
            (r'\s+', Text),

            # escape
            (r'\\', Punctuation),

            # value-index gets missed...
            (r'\.', Operator),

            # default is Text
            (r'.', Text)
        ],
        'multiline-comment': [
            (r'#\*', Comment.Multiline, '#push'),
            (r'\*#', Comment.Multiline, '#pop'),
            (r'#\|', Comment.Multiline, 'multiline-sl-comment'),
            (r'[^#*]+', Comment.Multiline),
            (r'[#*]', Comment.Multiline),
        ],
        'multiline-sl-comment': [
            (r'#\|', Comment.Multiline, '#push'),
            (r'\|#', Comment.Multiline, '#pop'),
            (r'#\*', Comment.Multiline, 'multiline-comment'),
            (r'[^#|]+', Comment.Multiline),
            (r'[#|]', Comment.Multiline),
        ],
        'sexp-comment': [
            (r'\(', Comment, '#push'),
            (r'\)', Comment, '#pop'),
            (r'[^()]+', Comment),
        ],
        'template': [
            (r'\$@?{', Punctuation, 'block'),
            (r'\$@?', Punctuation),
            include ('block')
        ],
        'pathname-template': [
            (r'}', Punctuation, '#pop'),
            (r'.', String.Double),
        ],
        'interp-string': [
            (r'}', Punctuation, '#pop'),
            # The examples use %
            (r'[\$%]{', Punctuation, 'block'),
            (r'[\$%]', Punctuation),
            (r'.', String.Double),
        ],
        'bitset': [
            (r'}', Punctuation, '#pop'),
            (r'.', Literal.Number),
        ],
        'array': [
            (r']', Punctuation, '#pop'),
            (r'.', Literal.Number),
        ],
        'hash': [
            (r'}', Punctuation, '#pop'),
            (r'.', Literal.Number),
        ],
        'block': [
            (r'{', Punctuation, '#push'),
            (r'}', Punctuation, '#pop'),
            include ('root'),
        ]
    }


class IdioConsoleLexer(Lexer):
    """
    Lexer for simplistic Idio interactive sessions.
    """

    name = 'Idio Console Session'
    aliases = ['idio-console']
    filenames = ['*.idio-console']

    line_re = re.compile('.*?\n')

    prompted_re = re.compile(
        r'^(\w+>\s+)(.*\n?)'
    )

    def get_tokens_unprocessed(self, text):
        innerlexer = IdioLexer (**self.options)

        pos = 0
        curcode = ''
        insertions = []
        backslash_continuation = False

        for match in self.line_re.finditer(text):
            line = match.group()
            if backslash_continuation:
                curcode += line
                backslash_continuation = curcode.endswith('\\\n')
                continue

            m = self.prompted_re.match(line)
            if m:
                # To support output lexers (say diff output), the output
                # needs to be broken by prompts whenever the output lexer
                # changes.
                if not insertions:
                    pos = match.start()

                insertions.append((len(curcode),
                                   [(0, Generic.Prompt, m.group(1))]))
                curcode += m.group(2)
                backslash_continuation = curcode.endswith('\\\n')
            else:
                if insertions:
                    toks = innerlexer.get_tokens_unprocessed(curcode)
                    for i, t, v in do_insertions(insertions, toks):
                        yield pos+i, t, v
                yield match.start(), Generic.Output, line
                insertions = []
                curcode = ''
        if insertions:
            for i, t, v in do_insertions(insertions,
                                         innerlexer.get_tokens_unprocessed(curcode)):
                yield pos+i, t, v


def setup(app):
    lexers['idio'] = IdioLexer()
    lexers['idio-console'] = IdioConsoleLexer()
