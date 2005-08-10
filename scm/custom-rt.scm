;;; custom-rt.scm: Partial customization support for runtime input
;;;                processes
;;;
;;; Copyright (c) 2003-2005 uim Project http://uim.freedesktop.org/
;;;
;;; All rights reserved.
;;;
;;; Redistribution and use in source and binary forms, with or without
;;; modification, are permitted provided that the following conditions
;;; are met:
;;; 1. Redistributions of source code must retain the above copyright
;;;    notice, this list of conditions and the following disclaimer.
;;; 2. Redistributions in binary form must reproduce the above copyright
;;;    notice, this list of conditions and the following disclaimer in the
;;;    documentation and/or other materials provided with the distribution.
;;; 3. Neither the name of authors nor the names of its contributors
;;;    may be used to endorse or promote products derived from this software
;;;    without specific prior written permission.
;;;
;;; THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
;;; ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
;;; IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
;;; ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
;;; FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
;;; DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
;;; OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
;;; HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
;;; LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
;;; OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
;;; SUCH DAMAGE.
;;;;

;; This file provides partial custom definition support for runtime
;; input processes. The processes that wants full-featured custom API
;; such as uim-pref must overrides these definitions by loading
;; custom.scm.
;;
;; The name 'custom-rt' is not the best to represent this partial
;; functionality. Give me better name.  -- YamaKen 2005-01-14

;; TODO: write test-custom-rt.scm

(require "util.scm")
(require "key.scm")

(define custom-full-featured? #f)

(define-record 'custom-choice-rec
  '((sym   #f)
    (label "")
    (desc  "")))

(define custom-required-custom-files ())
(define custom-reload-group-syms ())
(define custom-rt-primary-groups ())
(define custom-set-hooks ())

(define custom-file-path
  (lambda (gsym)
    (let* ((group-name (symbol->string gsym))
	   (path (string-append (getenv "HOME")
				"/.uim.d/customs/custom-"
				group-name
				".scm")))
      path)))

(define prepend-new-reload-group-syms
  (lambda (gsym path)
    (set! custom-reload-group-syms
	  (cons (cons gsym 0) custom-reload-group-syms))))

(define update-gsym-mtime
  (lambda (gsym path)
    (set-cdr! (assq gsym custom-reload-group-syms)
	      (file-mtime path))
	      #t))

(define custom-load-updated-group-conf
  (lambda (gsym)
    (let ((path (custom-file-path gsym)))
      (if (not (memq gsym (map (lambda (x) (car x)) custom-reload-group-syms)))
	  (prepend-new-reload-group-syms gsym path))
      (if (= (file-mtime path)
	     (cdr (assq gsym custom-reload-group-syms)))
	  #t ; File isn't modified, no need to reload. 
	  (if (try-load path)
	      (update-gsym-mtime gsym path)
	      #f)))))

;; full implementation
;; This proc is existing for DUMB loading. No more processing such as
;; mtime comparation or history recording must not be added. Please
;; keep in mind responsibility separation, and don't alter an API
;; specification previously stabilized without discussion.
;;   -- YamaKen 2005-08-09
(define custom-load-group-conf
  (lambda (gsym)
    (try-load (custom-file-path gsym))))

;; full implementation
(define require-custom
  (lambda (filename)
    (let ((pre-groups (custom-list-primary-groups)))
      (require filename)
      (if (not (member filename custom-required-custom-files))
	  (set! custom-required-custom-files
		(cons filename custom-required-custom-files)))
      (let* ((post-groups (custom-list-primary-groups))
	     (new-groups (list-tail post-groups (length pre-groups))))
	(if (not (getenv "LIBUIM_VANILLA"))
	    (for-each custom-load-group-conf
		      (reverse new-groups)))))))

;; full implementation
(define custom-reload-customs
  (lambda ()
    (for-each load (reverse custom-required-custom-files))
    (custom-call-all-hook-procs custom-set-hooks)))

;; full implementation
(define custom-modify-key-predicate-names
  (lambda (keys)
    (map (lambda (key)
	   (if (symbol? key)
	       (symbolconc key '?)
	       key))
	 keys)))

;; lightweight implementation
(define custom-choice-range-reflect-olist-val
  (lambda (dst-sym src-sym indication-alist)
    #f))

;; full implementation
(define custom-rt-add-primary-groups
  (lambda (gsym)
    (if (not (member gsym custom-rt-primary-groups))
	(set! custom-rt-primary-groups
	      (cons gsym custom-rt-primary-groups)))))

;; lightweight implementation
(define custom-list-primary-groups
  (lambda ()
    (reverse custom-rt-primary-groups)))

;; TODO: write test
;; lightweight implementation
(define custom-add-hook
  (lambda (custom-sym hook-sym proc)
    (if (eq? hook-sym
	     'custom-set-hooks)
	(set! custom-set-hooks
	      (alist-replace (cons custom-sym proc)
			     custom-set-hooks)))))

;; TODO: write test
;; lightweight implementation
(define custom-call-hook-procs
  (lambda (sym hook)
    (let ((proc (assq sym hook)))
      (if proc
	  ((cdr proc))))))

;; TODO: write test
;; full implementation
(define custom-call-all-hook-procs
  (lambda (hook)
    (for-each (lambda (pair)
		((cdr pair)))
	      hook)))

;; lightweight implementation
(define define-custom-group
  (lambda (gsym label desc)
    #f))

;; lightweight implementation
(define custom-exist?
  (lambda (sym type)
    (symbol-bound? sym)))

;; lightweight implementation
(define custom-key-exist?
  (lambda (sym)
    (let ((key-sym (symbolconc sym '?)))
      (and (symbol-bound? sym)
	   (list? (symbol-value sym))
	   (symbol-bound? key-sym)
	   (procedure? (symbol-value key-sym))))))

;; lightweight implementation
(define custom-value
  (lambda (sym)
    (symbol-value sym)))

;; TODO: rewrite test
;; lightweight implementation
(define custom-set-value!
  (lambda (sym val)
    (and (cond
	  ((custom-key-exist? sym)
	   (set-symbol-value! sym val)
	   (let ((key-val (custom-modify-key-predicate-names val)))
	     (eval (list 'define (symbolconc sym '?)
			 (list 'make-key-predicate (list 'quote key-val)))
		   toplevel-env))
	   #t)
	  ((custom-exist? sym #f)
	   (set-symbol-value! sym val)
	   #t)
	  (else
	   #f))
	 (begin
	   (custom-call-hook-procs sym custom-set-hooks)
	   #t))))

;; TODO: rewrite test
;; lightweight implementation
(define define-custom
  (lambda (sym default groups type label desc)
    (custom-rt-add-primary-groups (car groups))
    (if (not (custom-exist? sym type))
	(begin
	  (let ((quoted-default (if (or (symbol? default)
					(list? default))
				    (list 'quote default)
				    default)))
	    (eval (list 'define sym quoted-default)
		  toplevel-env)
	    (if (custom-key-exist? sym)
		;; already define-key'ed in ~/.uim
		(custom-call-hook-procs sym custom-set-hooks)
		(begin
		  (if (eq? (car type)
			   'key)
		      (eval (list 'define (symbolconc sym '?) list)
			    toplevel-env))
		  (custom-set-value! sym default))))))))  ;; to apply hooks

;; lightweight implementation
;; warning: no validation performed
(define custom-prop-update-custom-handler
  (lambda (context custom-sym val)
    (custom-set-value! custom-sym val)))

;; custom-reload-configs can switch its procedure definition from 2
;; implementations. custom-reload-customs is selectable since the
;; latter new code breaks the semantics of custom variable
;; broadcasting.
;;
;; For example, an arbitrary uim-enabled process can update a custom
;; variable by its own code without any helper message passing. In
;; such case, the previously defined broadcasting behavior overwrites
;; the variable locally modified even if the corresponding custom file
;; is not updated.
;;
;; To make the latter code default, a discussion is required.
;;   -- YamaKen 2005-08-09
(define custom-reload-configs
  (if #f
      custom-reload-customs  ;; original behavior
      (lambda ()
	(let ((group-syms (map (lambda (x) (car x)) custom-reload-group-syms)))
	  (if (null? group-syms)
	      #f ;; No file should be loaded.
	      (for-each custom-load-updated-group-conf
			(reverse group-syms)))))))
