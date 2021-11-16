;; https://www.emacswiki.org/emacs/ModeTutorial

;; and hints/clues from scheme-mode (implying lisp-mode) and ...

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
   (list (concat "\\<\\(define-\\(infix\\|postfix\\)-operator\\)\\>"
		 "[ \t]+\\([^[:space:]]+\\)[ \t]+\\([[:digit:]]+\\)"
		 )
	 '(1 font-lock-keyword-face)
	 '(4 (cond ((match-beginning 1) font-lock-function-name-face)
		   ((match-beginning 3) font-lock-keyword-face)
		   ((match-beginning 4) font-lock-constant-face)
		   (t font-lock-type-face))
	     nil t))
   (list (concat "\\<\\(define\\(\\|-template\\)\\)\\>"
		 "[ \t]*"
		 "\\(\"[^\"]*\"\\)?"
		 "("
		 "\\([^[:space:])]+\\)?")
	 '(1 font-lock-keyword-face)
	 '(4 (cond ((match-beginning 1) font-lock-function-name-face)
		   ((match-beginning 3) font-lock-doc-face)
		   ((match-beginning 4) font-lock-variable-name-face)
		   (t font-lock-type-face))
	     nil t))
   (list (concat "\\(?:[^-]\\)\\<\\(function\\)\\>\\(?:[^-]\\)"
		 "[ \t]*(")
	 '(1 font-lock-keyword-face)
	 '(3 font-lock-keyword-face))
   )
  "Subdued expressions to highlight in Idio modes.")

(defconst idio-font-lock-keywords-2
  (append idio-font-lock-keywords-1
	  (list
	   ;;
	   ;; Control structures.
	   (cons
	    (concat
	     "\\<" (regexp-opt
		    '(
		      ;; these should be the specials in idio_meaning
		      ;; in evaluate.c and other "important" functions
		      ;; in the bootstrap rather than general
		      ;; functions
		      "begin" "and" "or" "not"
		      "escape"
		      "quote" "quasiquote"
		      "function" "lambda"
		      "if" "cond"
		      "eq"
		      "block"
		      "dynamic" "dynamic-let" "dynamic-unset"
		      "environ-let" "environ-unset"
		      "trap"
		      "include"

		      ;; call-cc.idio
		      "call/cc" "values" "call-with-values" "call-with-current-continuation" "dynamic-wind" "unwind-protect" "with-condition-handler"
		      ;; closure.idio
		      "setter"
		      ;; command.idio
		      "fg-job" "bg-job" "wait"
		      ;; common.idio
		      "load" "with-values-from"
		      ;; condition.idio
		      "define-condition-type-accessors-only" "define-condition-type" "condition"
		      "condition-report"
		      ;; doc.idio
		      "help"
		      ;; libc-wrap.idio
		      "make-tmp-file-handle" "make-tmp-dir"
		      "getrlimit" "setrlimit"
		      ;; module.idio
		      "define-module" "module" "import" "export"
		      ;; path.idio
		      "sort_size" "sort_atime" "sort_mtime" "sort_ctime"
		      ;; SRFI-95.idio
		      "merge" "merge!" "sort!" "sort"
		      ;; struct.idio
		      "define-struct" "export-struct" "define-struct-accessors-only" "export-struct-accessors-only"


		      ;; s9.idio
		      "append" "if\+" "display\*" "edisplay\*"
		      "case" "do" "delay" "define-syntax"
		      "call-with-input-file" "call-with-output-file"
		      "do" "else" "for-each"
		      "let-values" "let*-values"
		      ) t)
	     "\\>") 1)

	   ;; #<THING ...> shouldn't be in code!
	   '("#<[^>]+>" . font-lock-warning-face)
	   ;; #t #n etc.
	   '("#\\sw+\\>" . font-lock-builtin-face)
	   ;; #\a #\newline
	   '("#\\\\[^[:space:]]+\\>" . font-lock-constant-face)
	   ;; :some-keyword-name
	   '(":[^[:space:]]+\\>" . font-lock-keyword-face)
	   ;; ^some-condition-name
	   '("\\^[^[:space:]]+\\>" . font-lock-type-face)
	   ;; some-function-name!
	   '("[^[:space:]]+!" . font-lock-warning-face)
	   ))
  "Gaudy expressions to highlight in Idio modes.")

(defvar idio-font-lock-keywords idio-font-lock-keywords-2
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
    (modify-syntax-entry ?{  "(}  " st)
    (modify-syntax-entry ?}  "){  " st)
    (modify-syntax-entry ?\| "_   " st)
    (modify-syntax-entry ?\! "_   " st)

    ;; Other atom delimiters
    (modify-syntax-entry ?\( "()  " st)
    (modify-syntax-entry ?\) ")(  " st)
    ;; It's used for single-line comments as well as for #;(...) sexp-comments.
    (modify-syntax-entry ?\; "<"    st)
    (modify-syntax-entry ?\" "\"   " st)
    (modify-syntax-entry ?'  "'   " st)
    (modify-syntax-entry ?`  "'   " st)
    (modify-syntax-entry ?\$ "'   " st)

    ;; Special characters
    (modify-syntax-entry ?,  "'   " st)
    (modify-syntax-entry ?@  "'   " st)
    (modify-syntax-entry ?#  "' 14" st)
    (modify-syntax-entry ?\| "_ 23bn" st)
    (modify-syntax-entry ?\* "_ 23bn" st)
    (modify-syntax-entry ?\\ "\\   " st)
    st)
  "Syntax table for idio-mode")

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
  (let* ((ppss (save-excursion
		 (beginning-of-line)
		 (syntax-ppss)))
	 (sexp-count (elt ppss 0))
	 (point-limit (elt ppss 1))
	 (current-indent (current-indentation))
	 (target-indent current-indent))
    (let (backslash-indent
	  formatted-indent
	  comment-indent
	  brace-dedent
	  brace-indent
	  paren-dedent
	  paren-indent)
      (progn
	(let ((looking t)
	      last-line-with-backslash)
	  (save-excursion
	    (while looking
	      (forward-line -1)
	      (goto-char (line-end-position))
	      (if (eq (char-before) ?\\)
		  (setq last-line-with-backslash (line-number-at-pos))
		(setq looking nil)))
	    (if last-line-with-backslash
		(condition-case nil
		    (progn
		      (forward-line)
		      (goto-char (line-end-position))
		      ;; the backslash is one sexp and we
		      ;; want to go back another
		      (backward-sexp 2)
		      (setq backslash-indent (current-column))
		      ;; is this an if+ statement?
		      (backward-sexp)
		      (if (eq last-line-with-backslash (line-number-at-pos))
			  (let ((possible-indent (current-column)))
			    (progn
			      (backward-sexp)
			      (if (and (eq last-line-with-backslash (line-number-at-pos))
				       (or (and point-limit
						(> (point) point-limit))
					   t))
				  (if (looking-at "if\+")
				      (setq backslash-indent possible-indent)))))))
		  ((scan-error)
		   nil)))))
	(if point-limit
	    (save-excursion
	      (goto-char point-limit)
	      (cond
	       ((elt ppss 3)
		(setq formatted-indent t))
	       ((elt ppss 4)
		(setq comment-indent (save-excursion
				       (goto-char (elt ppss 8))
				       (current-indentation))))	       
	       ((looking-at "{")
		(progn
		  ;; nominal indent is current-indentation +
		  ;; default-indent
		  ;(setq brace-indent (+ (current-indentation) idio-mode-indent))
		  ;; however, if this is an embedded function
		  ;; then the indent should be the default indent
		  ;; forward from the start of "function"
		  (or
		   ;; #S{ formatted string }
		   (condition-case nil
		       (progn
			 (save-excursion
			   (backward-sexp 1)
			   (if (looking-at "#S")
			       (setq formatted-indent t))))
		     ((scan-error)
		      nil))
		   ;; ^[[:space:]]*{
		   (condition-case nil
		       (progn
			 (save-excursion
			   (beginning-of-line)
			   (if (looking-at "[[:space:]]*{")
			       (setq brace-indent (+ (current-indentation) idio-mode-indent)))))
		     ((scan-error)
		      nil))
		   ;; define/function (with docstr)/if alternative/do
		   (condition-case nil
		       (progn
			 (save-excursion
			   (backward-sexp 3)
			   (if (looking-at "\\(define\\|function\\|if\\|do\\)")
			       (setq brace-indent (+ (current-indentation) idio-mode-indent)))))
		     ((scan-error)
		      nil))
		   ;; define/function/if consequent
		   (condition-case nil
		       (progn
			 (save-excursion
			   (backward-sexp 2)
			   (if (looking-at "\\(define\\|function\\|if\\)")			       
			       (setq brace-indent (+ (current-indentation) idio-mode-indent)))))
		     ((scan-error)
		      nil))
		   ;; cond clause
		   (condition-case nil
		       (progn
			 (save-excursion
			   (backward-sexp 1)
			   (setq brace-indent (+ (current-indentation) idio-mode-indent))))
		     ((scan-error)
		      nil)))
		  ))
	       ((looking-at "(")
		(progn
		  ;; nominal indent 1 forward from ?\( -- the
		  ;; forward-char is important otherwise the
		  ;; upcoming forward-sexp will (try to) jump
		  ;; over the entire sexp rather than step over
		  ;; sexps inside the (s
		  ;;
		  ;; We can indent if the next word is "function"
		  ;; where (it looks like) the function isn't
		  ;; using a block
		  (forward-char)
		  (setq paren-indent (+ (current-column) 1))
		  (cond
		   ((save-excursion
		      (progn
			(beginning-of-line)
			(if (looking-at "define-syntax")
			    (setq paren-indent idio-mode-indent)))))
		   ((looking-at "\\(cond[[:space:]]*$\\)")
		    (setq paren-indent (current-column)))
		   ((looking-at "\\(define\\|function\\|if\\|do\\|case\\|cond\\|syntax-rules\\)")
		    (setq paren-indent (+ (current-indentation) idio-mode-indent -1)))
		   (t
		    (let ((paren-line (line-number-at-pos))
			  (not-done t))
		      (condition-case nil
			  (while not-done
			    (progn
			      (forward-sexp)
			      (if (eq paren-line (line-number-at-pos))
				  (setq paren-indent (save-excursion
						       (backward-sexp)
						       (current-column)))
				(setq not-done nil))))
			((scan-error)
			 ;; this should be
			 ;; forward-sexp hitting the
			 ;; ?\) or eof or ...
			 nil))))))))))
	(setq brace-dedent (save-excursion
			     (progn
			       (beginning-of-line)
			       (if (looking-at "\\s-*}")
				   (condition-case nil
				       (progn
					 ;; skip back over this block
					 ;; for the blocks own
					 ;; indentation
					 (skip-chars-forward "[:space:]}")
					 (backward-sexp)
					 (or (condition-case nil
						 (progn
						   (backward-sexp 2)
						   (if (looking-at "\\(define\\|function\\|if\\|do\\|case\\|cond\\)")
						       (current-indentation)))
					       ((scan-error)
						nil))
					     (condition-case nil
						 (progn
						   (backward-sexp 1)
						   (current-indentation))
					       ((scan-error)
						nil))
					     (current-indentation)))
				     ((scan-error)
				      nil))))))
	(setq paren-dedent (save-excursion
			     (progn
			       (beginning-of-line)
			       (if (looking-at "\\s-*)")
				   (condition-case nil
				       (progn
					 ;; skip back over this sexp
					 ;; for the blocks own
					 ;; indentation
					 (skip-chars-forward "[:space:])")
					 (backward-sexp)
					 (current-indentation))
				     ((scan-error)
				      nil))))))
	
	(cond
	 (comment-indent
	  (progn
	    (setq target-indent comment-indent)
	    (message "#*")))
	 (formatted-indent
	  (progn
	    (setq target-indent nil)
	    (message "#S")))
	 (backslash-indent
	  (progn
	    (setq target-indent backslash-indent)
	    (message "\\: %s %s" current-indent target-indent)))
	 (brace-dedent
	  (progn
	    (setq target-indent brace-dedent)
	    (message "}: %s %s" current-indent target-indent)))
	 (brace-indent
	  (progn
	    (setq target-indent brace-indent)
	    (message "{: %s %s" current-indent target-indent)))
	 (paren-dedent
	  (progn
	    (setq target-indent (if (eq 0 sexp-count)
				    sexp-count
				  (* idio-mode-indent (- sexp-count 1))))
	    (setq target-indent paren-dedent)
	    (message "): %s %s %s" current-indent target-indent sexp-count)))
	 (paren-indent
	  (progn
	    (setq target-indent paren-indent)
	    (message "(: %s %s" current-indent target-indent)))
	 (t
	  (progn
	    (setq target-indent (* idio-mode-indent sexp-count))
	    (message "-: %s %s" current-indent target-indent))))))
    (if target-indent
	(if (> current-indent target-indent)
	    (save-excursion
	      (beginning-of-line)
	      (delete-horizontal-space)
	      (indent-to target-indent))
	  (if (not (eq current-indent target-indent))
	      (progn
		(save-excursion
		  (beginning-of-line)
		  (delete-horizontal-space)
		  (indent-to target-indent))))))
    (skip-chars-forward "[:space:]")))

(defvar idio-func-regexp
  (regexp-opt '("define" "define*" "define-" "function" "function*") t))

(defvar idio-defun-regexp
  (concat
   "\\(^[[:space:]]*\\|\\(:[=+]\\|=\\)[[:space:]]*\\|(\\)"
   idio-func-regexp))

;; beginning/end code re-imagined from tcl.el
(defun idio-beginning-of-defun-function (&optional arg)
  "`beginning-of-defun-function' for Idio mode."
  (when (or (not arg) (= arg 0))
    (setq arg 1))
  (let* ((search-fn (if (> arg 0)
                        ;; Positive arg means to search backward.
                        #'re-search-backward
                      #'re-search-forward))
         (arg (abs arg))
         (result t))
    (while (and (> arg 0) result)
      (unless (funcall search-fn idio-defun-regexp nil t)
        (setq result nil))
      (setq arg (1- arg)))
    result))

(defun idio-end-of-defun-function ()
  "`end-of-defun-function' for Idio mode."
  (if (not (condition-case nil
	       (re-search-forward idio-func-regexp (line-end-position) nil)
	     (search-failed
	      t)))
      (idio-beginning-of-defun-function))
  (condition-case nil
      (progn
	;; args
	(forward-sexp)
	;; docstr?
	(if (looking-at "[[:space:]]*\"")
	    (progn
	      (message "yo!")
	      (forward-sexp))
	  (message "no!"))
	;; body
	(forward-sexp))
    (scan-error
     (goto-char (point-max)))))

(defvar idio-mode-default-indent 2)

;; Yuk - this appears to descend through scheme-syntax-propertize and
;; scheme-syntax-propertize-comment and into syntax.el -- it is
;; required for #; to work correctly
(defconst idio-sexp-comment-syntax-table
  (let ((st (make-syntax-table idio-mode-syntax-table)))
    (modify-syntax-entry ?\; "." st)
    (modify-syntax-entry ?\n " " st)
    (modify-syntax-entry ?#  "'" st)
    st))

(defun idio-mode ()
  "Major mode for editing Idio source files"
  (interactive)
  (kill-all-local-variables)
  (set-syntax-table idio-mode-syntax-table)
  (use-local-map idio-mode-map)
  (set (make-local-variable 'font-lock-defaults) '(idio-font-lock-keywords))
  (set (make-local-variable 'indent-line-function) 'idio-indent-line)
  (set (make-local-variable 'lisp-indent-function) 'idio-indent-function)
  (set (make-local-variable 'idio-mode-indent) idio-mode-default-indent)

  ;; borrowed from scheme.el
  (setq-local paragraph-ignore-fill-prefix t)
  (setq-local fill-paragraph-function 'lisp-fill-paragraph)
  ;; Adaptive fill mode gets in the way of auto-fill,
  ;; and should make no difference for explicit fill
  ;; because lisp-fill-paragraph should do the job.
  (setq-local adaptive-fill-mode nil)
  (setq-local parse-sexp-ignore-comments t)
  (setq-local outline-regexp ";;; \\|(....")
  (setq-local add-log-current-defun-function #'lisp-current-defun-name)
  (setq-local comment-start ";")
  (setq-local comment-add 1)
  (setq-local comment-start-skip ";+[ \t]*")
  (setq-local comment-use-syntax t)
  (setq-local comment-column 40)

  (setq-local beginning-of-defun-function #'idio-beginning-of-defun-function)
  (setq-local end-of-defun-function #'idio-end-of-defun-function)

  (setq major-mode 'idio-mode)
  (setq mode-name "Idio")
  (run-hooks 'idio-mode-hook))

(provide 'idio-mode)
