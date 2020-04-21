;; https://www.emacswiki.org/emacs/ModeTutorial

(require 'scheme)

(defvar idio-mode-hook nil)

(defvar idio-mode-map
  (let ((map (make-keymap)))
    (define-key map "\C-j" 'newline-and-indent)
    map)
  "Keymap for Idio major mode")

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.idio\\'" . idio-mode))

(defconst idio-font-lock-keywords-1
  (list
     ;;
     ;; Declarations.  Hannes Haug <hannes.haug@student.uni-tuebingen.de> says
     ;; this works for SOS, STklos, SCOOPS, Meroon and Tiny CLOS.
     (list (concat "\\<\\(define\\)\\>"
                   ;; Any whitespace and declared object.
                   ;; The "(*" is for curried definitions, e.g.,
                   ;;  (define ((sum a) b) (+ a b))
                   "[ \t]*(*"
                   "\\(\\sw+\\)?")
           '(1 font-lock-keyword-face)
           '(2 (cond ((match-beginning 1) font-lock-function-name-face)
                     ((match-beginning 2) font-lock-variable-name-face)
                     (t font-lock-type-face))
               nil t))
     ;;
     ;; Declarations.  Hannes Haug <hannes.haug@student.uni-tuebingen.de> says
     ;; this works for SOS, STklos, SCOOPS, Meroon and Tiny CLOS.
     (list (concat "\\<define\\*?\\("
                   ;; Function names.
                   "\\(\\|-public\\|-method\\|-generic\\(-procedure\\)?\\)\\|"
                   ;; Macro names, as variable names.  A bit dubious, this.
                   "\\(-syntax\\|-macro\\)\\|"
                   ;; Class names.
                   "-class"
                   ;; Guile modules.
                   "\\|-module"
                   "\\)\\)\\>"
                   ;; Any whitespace and declared object.
                   ;; The "(*" is for curried definitions, e.g.,
                   ;;  (define ((sum a) b) (+ a b))
                   "[ \t]*(*"
                   "\\(\\sw+\\)?")
           '(1 font-lock-keyword-face)
           '(6 (cond ((match-beginning 3) font-lock-function-name-face)
                     ((match-beginning 5) font-lock-variable-name-face)
                     (t font-lock-type-face))
               nil t))
     )
  "Subdued expressions to highlight in Idio modes.")

(defconst idio-font-lock-keywords-2
  (append idio-font-lock-keywords-1
   (list
      ;;
      ;; Control structures.
      (cons
       (concat
        "(" (regexp-opt
             '("begin" "call-with-current-continuation" "call/cc"
               "call-with-input-file" "call-with-output-file" "case" "cond"
               "do" "else" "for-each" "if" "lambda" "λ"
               "let" "let*" "let-syntax" "letrec" "letrec-syntax"
               ;; R6RS library subforms.
               "export" "import"
               ;; SRFI 11 usage comes up often enough.
               "let-values" "let*-values"
               ;; Hannes Haug <hannes.haug@student.uni-tuebingen.de> wants:
               "and" "or" "delay" "force"
               ;; Stefan Monnier <stefan.monnier@epfl.ch> says don't bother:
               ;;"quasiquote" "quote" "unquote" "unquote-splicing"
	       "map" "syntax" "syntax-rules"
	       ;; For R7RS
	       "when" "unless" "letrec*" "include" "include-ci" "cond-expand"
	       "delay-force" "parameterize" "guard" "case-lambda"
	       "syntax-error" "only" "except" "prefix" "rename" "define-values"
	       "define-record-type" "define-library"
	       "include-library-declarations"
	       ;; SRFI-8
	       "receive"
	       ) t)
        "\\>") 1)
      ;;
      ;; It wouldn't be Idio w/o named-let.
      '("(let\\s-+\\(\\sw+\\)"
        (1 font-lock-function-name-face))
      ;;
      ;; David Fox <fox@graphics.cs.nyu.edu> for SOS/STklos class specifiers.
      '("\\<<\\sw+>\\>" . font-lock-type-face)
      ;;
      ;; Idio `:' and `#:' keywords as builtins.
      '("\\<#?:\\sw+\\>" . font-lock-builtin-face)
      ;; R6RS library declarations.
      '("(\\(\\<library\\>\\)\\s-*(?\\(\\sw+\\)?"
        (1 font-lock-keyword-face)
        (2 font-lock-type-face))
      ))
  "Gaudy expressions to highlight in Idio modes.")

(defvar idio-font-lock-keywords idio-font-lock-keywords-1
  "Default expressions to highlight in Idio modes.")

(defvar idio-mode-syntax-table
  (let ((st (make-syntax-table))
        (i 0))
    ;; Symbol constituents
    ;; We used to treat chars 128-256 as symbol-constituent, but they
    ;; should be valid word constituents (Bug#8843).  Note that valid
    ;; identifier characters are Scheme-implementation dependent.
    (while (< i ?0)
      (modify-syntax-entry i "_   " st)
      (setq i (1+ i)))
    (setq i (1+ ?9))
    (while (< i ?A)
      (modify-syntax-entry i "_   " st)
      (setq i (1+ i)))
    (setq i (1+ ?Z))
    (while (< i ?a)
      (modify-syntax-entry i "_   " st)
      (setq i (1+ i)))
    (setq i (1+ ?z))
    (while (< i 128)
      (modify-syntax-entry i "_   " st)
      (setq i (1+ i)))

    ;; Whitespace
    (modify-syntax-entry ?\t "    " st)
    (modify-syntax-entry ?\n ">   " st)
    (modify-syntax-entry ?\f "    " st)
    (modify-syntax-entry ?\r "    " st)
    (modify-syntax-entry ?\s "    " st)

    ;; These characters are delimiters but otherwise undefined.
    ;; Brackets and braces balance for editing convenience.
    (modify-syntax-entry ?\[ "(]  " st)
    (modify-syntax-entry ?\] ")[  " st)
    (modify-syntax-entry ?{ "(}  " st)
    (modify-syntax-entry ?} "){  " st)
    (modify-syntax-entry ?\| "\" 23bn" st)
    ;; Guile allows #! ... !# comments.
    ;; But SRFI-22 defines the comment as #!...\n instead.
    ;; Also Guile says that the !# should be on a line of its own.
    ;; It's too difficult to get it right, for too little benefit.
    ;; (modify-syntax-entry ?! "_ 2" st)

    ;; Other atom delimiters
    (modify-syntax-entry ?\( "()  " st)
    (modify-syntax-entry ?\) ")(  " st)
    ;; It's used for single-line comments as well as for #;(...) sexp-comments.
    (modify-syntax-entry ?\; "<"    st)
    (modify-syntax-entry ?\" "\"   " st)
    (modify-syntax-entry ?' "'   " st)
    (modify-syntax-entry ?` "'   " st)

    ;; Special characters
    (modify-syntax-entry ?, "'   " st)
    (modify-syntax-entry ?@ "'   " st)
    (modify-syntax-entry ?# "' 14" st)
    (modify-syntax-entry ?\\ "\\   " st)
    st)
  "Syntax table for idio-mode")

(defun lisp-indent-line (&optional indent)
  "Indent current line as Lisp code."
  (interactive)
  (let ((pos (- (point-max) (point)))
        (indent (progn (beginning-of-line)
                       (or indent (calculate-lisp-indent (lisp-ppss)))))
	(shift-amt nil)
	(beg (progn (beginning-of-line) (point))))
    (skip-chars-forward " \t")
    (if (or (null indent) (looking-at "\\s<\\s<\\s<"))
	;; Don't alter indentation of a ;;; comment line
	;; or a line that starts in a string.
        ;; FIXME: inconsistency: comment-indent moves ;;; to column 0.
	(goto-char (- (point-max) pos))
      (if (and (looking-at "\\s<") (not (looking-at "\\s<\\s<")))
	  ;; Single-semicolon comment lines should be indented
	  ;; as comment lines, not as code.
	  (progn (indent-for-comment) (forward-char -1))
	(if (listp indent) (setq indent (car indent)))
	(setq shift-amt (- indent (current-column)))
	(if (zerop shift-amt)
	    nil
	  (delete-region beg (point))
	  (indent-to indent)))
      ;; If initial point was within line's indentation,
      ;; position after the indentation.  Else stay at same point in text.
      (if (> (- (point-max) pos) (point))
	  (goto-char (- (point-max) pos))))))

(defvar calculate-lisp-indent-last-sexp)

(defun idio-indent-function (indent-point state)
  "Scheme mode function for the value of the variable `lisp-indent-function'.
This behaves like the function `lisp-indent-function', except that:

i) it checks for a non-nil value of the property `idio-indent-function'
\(or the deprecated `scheme-indent-hook'), rather than `lisp-indent-function'.

ii) if that property specifies a function, it is called with three
arguments (not two), the third argument being the default (i.e., current)
indentation."
  (let ((normal-indent (current-column)))
    (goto-char (1+ (elt state 1)))
    (parse-partial-sexp (point) calculate-lisp-indent-last-sexp 0 t)
    (if (and (elt state 2)
             (not (looking-at "\\sw\\|\\s_")))
        ;; car of form doesn't seem to be a symbol
        (progn
          (if (not (> (save-excursion (forward-line 1) (point))
                      calculate-lisp-indent-last-sexp))
              (progn (goto-char calculate-lisp-indent-last-sexp)
                     (beginning-of-line)
                     (parse-partial-sexp (point)
                                         calculate-lisp-indent-last-sexp 0 t)))
          ;; Indent under the list or under the first sexp on the same
          ;; line as calculate-lisp-indent-last-sexp.  Note that first
          ;; thing on that line has to be complete sexp since we are
          ;; inside the innermost containing sexp.
          (backward-prefix-chars)
          (current-column))
      (let ((function (buffer-substring (point)
                                        (progn (forward-sexp 1) (point))))
            method)
        (setq method (or (get (intern-soft function) 'idio-indent-function)
                         (get (intern-soft function) 'scheme-indent-hook)))
        (cond ((or (eq method 'defun)
                   (and (null method)
                        (> (length function) 3)
                        (string-match "\\`def" function)))
               (lisp-indent-defform state indent-point))
              ((integerp method)
               (lisp-indent-specform method state
                                     indent-point normal-indent))
              (method
                (funcall method state indent-point normal-indent)))))))

;; (put 'begin 'idio-indent-function 0), say, causes begin to be indented
;; like defun if the first form is placed on the next line, otherwise
;; it is indented like any other form (i.e. forms line up under first).

(put 'begin 'idio-indent-function 0)
(put 'case 'idio-indent-function 1)
(put 'delay 'idio-indent-function 0)
(put 'do 'idio-indent-function 2)
(put 'lambda 'idio-indent-function 1)
(put 'let 'idio-indent-function 'scheme-let-indent)
(put 'let* 'idio-indent-function 1)
(put 'letrec 'idio-indent-function 1)
(put 'let-values 'idio-indent-function 1) ; SRFI 11
(put 'let*-values 'idio-indent-function 1) ; SRFI 11
(put 'sequence 'idio-indent-function 0) ; SICP, not r4rs
(put 'let-syntax 'idio-indent-function 1)
(put 'letrec-syntax 'idio-indent-function 1)
(put 'syntax-rules 'idio-indent-function 1)
(put 'syntax-case 'idio-indent-function 2) ; not r5rs
(put 'library 'idio-indent-function 1) ; R6RS

(put 'call-with-input-file 'idio-indent-function 1)
(put 'with-input-from-file 'idio-indent-function 1)
(put 'with-input-from-port 'idio-indent-function 1)
(put 'call-with-output-file 'idio-indent-function 1)
(put 'with-output-to-file 'idio-indent-function 1)
(put 'with-output-to-port 'idio-indent-function 1)
(put 'call-with-values 'idio-indent-function 1) ; r5rs?
(put 'dynamic-wind 'idio-indent-function 3) ; r5rs?

;; R7RS
(put 'when 'idio-indent-function 1)
(put 'unless 'idio-indent-function 1)
(put 'letrec* 'idio-indent-function 1)
(put 'parameterize 'idio-indent-function 1)
(put 'define-values 'idio-indent-function 1)
(put 'define-record-type 'idio-indent-function 1) ;; is 1 correct?
(put 'define-library 'idio-indent-function 1)

;; SRFI-8
(put 'receive 'idio-indent-function 2)

(defun idio-indent-line ()
  "Indent current line as Idio code"
  (interactive)
  ;(beginning-of-line)
  (let* ((ppss (syntax-ppss))
	 (target-indent )
	 (current-indent (current-indentation)))
    (if (elt ppss 0)
	(let (backslash-indent
	      brace-indent
	      paren-indent)
	  (progn
	    (save-excursion (forward-line -1)
			    (goto-char (line-end-position))
			    (if (eq (char-before) ?\\)
				(progn
				  ;; the backslash is one sexp and we
				  ;; want to go back another
				  (backward-sexp 2)
				  (setq backslash-indent (current-column)))))
	    (save-excursion (goto-char (elt ppss 1))
			    (setq brace-indent (looking-at "{"))
			    (setq paren-indent (and (looking-at "(")
						    (1+ (current-column)))))
	    
	    (cond (backslash-indent
		   (progn
		     ;(message "\\I: %s" backslash-indent)
		     (setq target-indent backslash-indent)))
		  (brace-indent
		   (progn
		     ;(message "{: %s" (elt ppss 0))
		     (setq target-indent (* idio-mode-indent (elt ppss 0)))))
		  (paren-indent
		   (progn
		     ;(message "(: %s" paren-indent)
		     (setq target-indent paren-indent)))))))
    (if (not (eq current-indent target-indent))
	(indent-to target-indent))))

(defvar idio-mode-default-indent 2)

(defun idio-mode ()
  "Major mode for editing Workflow Process Description Language files"
  (interactive)
  (kill-all-local-variables)
  (set-syntax-table idio-mode-syntax-table)
  (use-local-map idio-mode-map)
  (set (make-local-variable 'font-lock-defaults) '(idio-font-lock-keywords))
  (set (make-local-variable 'indent-line-function) 'idio-indent-line)
  (set (make-local-variable 'lisp-indent-function) 'idio-indent-function)
  (set (make-local-variable 'idio-mode-indent) idio-mode-default-indent)
  (setq major-mode 'idio-mode)
  (setq mode-name "Idio")
  (run-hooks 'idio-mode-hook))

(provide 'idio-mode)
