;;; libregnum-tests.el --- Tests for libregnum.el -*- lexical-binding: t; -*-

;; Copyright (C) 2025  Zach Podbielniak

;; This program is free software: you can redistribute it and/or modify
;; it under the terms of the GNU Affero General Public License as published by
;; the Free Software Foundation, either version 3 of the License, or
;; (at your option) any later version.
;;
;; This program is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU Affero General Public License for more details.
;;
;; You should have received a copy of the GNU Affero General Public License
;; along with this program.  If not, see <https://www.gnu.org/licenses/>.

;;; Commentary:

;; Run with:
;;   emacs -Q --batch -L editors/emacs -l libregnum-tests.el -f ert-run-tests-batch-and-exit
;;
;; The protocol tests use a fake editor served from this Emacs.  The
;; end-to-end test starts a real headless editor when $LRG_EDITOR names
;; the lrg-editor binary (e.g. build/release/lrg-editor); it is skipped
;; otherwise.

;;; Code:

(require 'ert)
(require 'libregnum)

;;; Fake editor ------------------------------------------------------------

(defvar libregnum-tests--server nil)
(defvar libregnum-tests--client nil)
(defvar libregnum-tests--requests nil
  "Requests the fake editor received, newest first.")

(defun libregnum-tests--reply (process message)
  "Send MESSAGE (an alist) to PROCESS as one line."
  (process-send-string process (concat (libregnum--json-encode message) "\n")))

(defun libregnum-tests--handle (process line)
  "Answer the request LINE from PROCESS like a small editor would."
  (let* ((request (libregnum--json-decode line))
         (id (alist-get 'id request))
         (method (alist-get 'method request)))
    (push request libregnum-tests--requests)
    (libregnum-tests--reply
     process
     (pcase method
       ("hello" `((id . ,id) (result . ((server . "libregnum-editor") (protocol . 1) (project . "/game")))))
       ("ping" `((id . ,id) (result . "pong")))
       ("node.tree"
        `((id . ,id)
          (result . [((guid . "g1") (name . "Root") (kind . "none") (visible . t) (locked . :json-false)
                      (children . 1)
                      (nodes . [((guid . "g2") (name . "Cube") (kind . "primitive") (visible . t)
                                 (locked . :json-false) (children . 0) (nodes . []))]))])))
       ("editor.info"
        `((id . ,id) (result . ((level . "/game/levels/main.rlevel") (dirty . :json-false) (nodes . 2)
                                (tool . "select") (selection . ((nodes . ["g2"]))) (clients . 1)))))
       ("broken" `((id . ,id) (error . ((code . "failed") (message . "it broke")))))
       (_ `((id . ,id) (result . t)))))))

(defun libregnum-tests--filter (process output)
  "Split OUTPUT from PROCESS into lines for the fake editor."
  (let ((pending (concat (or (process-get process 'pending) "") output)))
    (while (string-match "\n" pending)
      (let ((line (substring pending 0 (match-beginning 0))))
        (setq pending (substring pending (match-end 0)))
        (libregnum-tests--handle process line)))
    (process-put process 'pending pending)))

(defmacro libregnum-tests--with-fake-editor (&rest body)
  "Run BODY connected to a fake editor."
  (declare (indent 0))
  `(let* ((dir (make-temp-file "lrg-el" t))
          (socket (expand-file-name "editor-1.sock" dir))
          (libregnum-tests--requests nil))
     (unwind-protect
         (progn
           (setq libregnum-tests--server
                 (make-network-process :name "fake-editor" :family 'local :service socket :server t
                                       :noquery t
                                       :filter #'libregnum-tests--filter
                                       :log (lambda (_server client _msg)
                                              (setq libregnum-tests--client client))))
           (let ((libregnum-socket-directory dir))
             (libregnum-connect)
             ,@body))
       (libregnum-disconnect)
       (when libregnum-tests--server
         (delete-process libregnum-tests--server))
       (delete-directory dir t))))

(defun libregnum-tests--wait (predicate)
  "Spin the event loop until PREDICATE is true (or 3 seconds pass)."
  (let ((deadline (+ (float-time) 3.0)))
    (while (and (not (funcall predicate)) (< (float-time) deadline))
      (accept-process-output nil 0.02))
    (funcall predicate)))

;;; Tests --------------------------------------------------------------------

(ert-deftest libregnum-test-json-roundtrip ()
  (let ((decoded (libregnum--json-decode
                  (libregnum--json-encode '((a . 1) (b . "x") (c . [1 2]) (d . :json-false) (e . t))))))
    (should (equal (alist-get 'a decoded) 1))
    (should (equal (alist-get 'b decoded) "x"))
    (should (equal (alist-get 'c decoded) [1 2]))
    (should (eq (alist-get 'd decoded) nil))
    (should (eq (alist-get 'e decoded) t))))

(ert-deftest libregnum-test-find-socket ()
  (let* ((dir (make-temp-file "lrg-el" t))
         (libregnum-socket nil)
         (libregnum-socket-directory dir))
    (unwind-protect
        (progn
          (should (null (libregnum-find-socket)))
          (write-region "" nil (expand-file-name "editor-10.sock" dir))
          (set-file-times (expand-file-name "editor-10.sock" dir) (time-subtract nil 100))
          (write-region "" nil (expand-file-name "editor-20.sock" dir))
          (write-region "" nil (expand-file-name "unrelated.sock" dir))
          (should (string-suffix-p "editor-20.sock" (libregnum-find-socket))))
      (delete-directory dir t))))

(ert-deftest libregnum-test-hello-declares-open-file ()
  (libregnum-tests--with-fake-editor
    (should (libregnum-connected-p))
    (let ((hello (car (last libregnum-tests--requests))))
      (should (equal (alist-get 'method hello) "hello"))
      (should (equal (alist-get 'capabilities (alist-get 'params hello)) ["open-file"]))
      (should (equal (alist-get 'client (alist-get 'params hello)) "emacs")))
    (should (equal (alist-get 'project libregnum--server-info) "/game"))))

(ert-deftest libregnum-test-request-and-error ()
  (libregnum-tests--with-fake-editor
    (should (equal (libregnum-request "ping") "pong"))
    (should-error (libregnum-request "broken") :type 'error)))

(ert-deftest libregnum-test-open-file-event ()
  (libregnum-tests--with-fake-editor
    (let* ((file (make-temp-file "lrg-script" nil ".lua" "line one\nline two\nline three\n"))
           (opened nil)
           (libregnum-open-file-function (lambda (f l c) (setq opened (list f l c)))))
      (unwind-protect
          (progn
            (libregnum-tests--reply libregnum-tests--client
                                    `((event . "open-file") (data . ((file . ,file) (line . 2) (col . 5)))))
            (should (libregnum-tests--wait (lambda () opened)))
            (should (equal opened (list file 2 5))))
        (delete-file file)))))

(ert-deftest libregnum-test-default-open-file-goes-to-line ()
  (let ((file (make-temp-file "lrg-script" nil ".lua" "one\ntwo\nthree four\n"))
        (libregnum-raise-frame nil))
    (unwind-protect
        (save-window-excursion
          (libregnum-default-open-file file 3 7)
          (should (equal (buffer-file-name) file))
          (should (= (line-number-at-pos) 3))
          (should (= (current-column) 6))
          (kill-buffer))
      (delete-file file))))

(ert-deftest libregnum-test-log-event ()
  (libregnum-tests--with-fake-editor
    (when (get-buffer libregnum-log-buffer-name)
      (kill-buffer libregnum-log-buffer-name))
    (libregnum-tests--reply libregnum-tests--client
                            '((event . "log") (data . ((level . "warning") (domain . "game") (message . "low fuel")))))
    (should (libregnum-tests--wait
             (lambda ()
               (and (get-buffer libregnum-log-buffer-name)
                    (with-current-buffer libregnum-log-buffer-name
                      (string-match-p "low fuel" (buffer-string)))))))))

(ert-deftest libregnum-test-outline ()
  (libregnum-tests--with-fake-editor
    (save-window-excursion
      (libregnum-outline)
      (with-current-buffer "*libregnum-outline*"
        (should (derived-mode-p 'libregnum-outline-mode))
        (should (string-match-p "main.rlevel" (buffer-string)))
        (should (string-match-p "^- Root" (buffer-string)))
        (should (string-match-p "^    Cube  primitive" (buffer-string)))
        ;; RET selects the node on the line in the editor.
        (goto-char (point-min))
        (search-forward "Cube")
        (libregnum-outline-select)
        (let ((req (car libregnum-tests--requests)))
          (should (equal (alist-get 'method req) "selection.set"))
          (should (equal (alist-get 'nodes (alist-get 'params req)) ["g2"])))
        (kill-buffer)))))

(ert-deftest libregnum-test-node-candidates ()
  (libregnum-tests--with-fake-editor
    (should (equal (libregnum--node-candidates) '(("Root" . "g1") ("Root/Cube" . "g2"))))))

(ert-deftest libregnum-test-rlevel-mode ()
  (should (eq (assoc-default "levels/arena.rlevel" auto-mode-alist #'string-match) 'libregnum-rlevel-mode))
  (should (eq (assoc-default "project.ryaml" auto-mode-alist #'string-match) 'libregnum-rlevel-mode))
  (with-temp-buffer
    (insert "nodes:\n  - name: Cube\n")
    (libregnum-rlevel-mode)
    (should (derived-mode-p 'libregnum-rlevel-mode))
    (should (equal comment-start "# "))))

;;; End to end -----------------------------------------------------------------

(ert-deftest libregnum-test-real-editor ()
  (let ((program (and (getenv "LRG_EDITOR") (expand-file-name (getenv "LRG_EDITOR")))))
    (skip-unless (and program (file-executable-p program)))
    (let* ((dir (make-temp-file "lrg-e2e" t))
           (project (expand-file-name "game" dir))
           (settings (expand-file-name "editor.yaml" dir))
           (socket (expand-file-name (format "lrg-e2e-%d.sock" (emacs-pid))
                                     (or (getenv "XDG_RUNTIME_DIR") "/tmp")))
           (proc nil))
      (unwind-protect
          (progn
            (should (= 0 (call-process program nil nil nil "--settings" settings
                                       "--new-project" project "--validate")))
            (setq proc (start-process "lrg-editor" nil program "--settings" settings
                                      "--headless" "--socket" socket project))
            (should (libregnum-tests--wait (lambda () (file-exists-p socket))))
            (libregnum-connect socket)
            (should (equal (libregnum-request "ping") "pong"))
            (should (= (length (libregnum--node-candidates)) 3))
            (libregnum-create-node "FromEmacs" "primitive" "cube")
            (should (assoc "FromEmacs" (libregnum--node-candidates)))
            (libregnum-undo)
            (should-not (assoc "FromEmacs" (libregnum--node-candidates)))
            (libregnum-redo)
            (libregnum-save)
            (with-temp-buffer
              (insert-file-contents (expand-file-name "levels/main.rlevel" project))
              (should (string-match-p "FromEmacs" (buffer-string))))
            (libregnum-request "editor.quit"))
        (libregnum-disconnect)
        (when (and proc (process-live-p proc))
          (kill-process proc))
        (delete-directory dir t)))))

(provide 'libregnum-tests)

;;; libregnum-tests.el ends here
