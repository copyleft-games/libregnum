;;; libregnum.el --- Emacs integration for the libregnum editor -*- lexical-binding: t; -*-

;; Copyright (C) 2025  Zach Podbielniak

;; Author: Zach Podbielniak
;; Version: 0.2.0
;; Package-Requires: ((emacs "28.1"))
;; Keywords: games, tools, processes
;; URL: https://gitlab.com/zachpodbielniak/libregnum

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

;; First-party Emacs integration for `lrgeditor', the libregnum level,
;; scene and world editor.
;;
;; The editor serves a JSON-lines protocol on a Unix socket
;; ($XDG_RUNTIME_DIR/libregnum/editor-PID.sock).  This package connects
;; to it and:
;;
;;  - opens files for the editor: scripts, level YAML and other code the
;;    editor would hand to $EDITOR are visited in this Emacs instead
;;    (the "open-file" capability), at the right line and column;
;;  - shows the level as an outline (`libregnum-outline'), with
;;    selection kept in sync both ways, and lets you rename, delete,
;;    create, move and edit nodes from Emacs;
;;  - inspects nodes as pretty-printed data (`libregnum-inspect');
;;  - drives the editor: save, undo/redo, play/pause/step/stop, run the
;;    game, any editor action by name (`libregnum-action'), validate the
;;    level into a `compilation-mode' buffer;
;;  - streams the editor log into *libregnum-log*;
;;  - provides `libregnum-rlevel-mode' for .rlevel/.rprefab/.ryaml files.
;;
;; Quick start:
;;
;;   (require 'libregnum)
;;   (libregnum-global-mode 1)          ; C-c L prefix, auto-mode for .rlevel
;;   M-x libregnum-start-editor RET     ; or start lrgeditor yourself, then
;;   M-x libregnum-connect RET
;;
;; In the editor, set Preferences > External Editor to "Emacs (client)"
;; or leave "auto" with EDITOR=emacsclient; while this package is
;; connected, files open here either way.

;;; Code:

(require 'cl-lib)
(require 'json)
(require 'subr-x)
(require 'compile)

;;; ==========================================================================
;;; Customization
;;; ==========================================================================

(defgroup libregnum nil
  "Integration with the libregnum editor."
  :group 'tools
  :prefix "libregnum-")

(defcustom libregnum-editor-program "lrgeditor"
  "The libregnum editor executable."
  :type 'string)

(defcustom libregnum-editor-arguments nil
  "Extra arguments for `libregnum-start-editor'."
  :type '(repeat string))

(defcustom libregnum-socket nil
  "Socket of the editor to connect to.
When nil, the most recently started editor's socket in
`libregnum-socket-directory' is used."
  :type '(choice (const :tag "Newest editor" nil) file))

(defcustom libregnum-socket-directory
  (expand-file-name "libregnum" (or (getenv "XDG_RUNTIME_DIR") temporary-file-directory))
  "Directory where editors create their sockets."
  :type 'directory)

(defcustom libregnum-request-timeout 5.0
  "Seconds to wait for a reply to a synchronous request."
  :type 'number)

(defcustom libregnum-open-file-function #'libregnum-default-open-file
  "Function called with FILE, LINE and COLUMN when the editor opens a file."
  :type 'function)

(defcustom libregnum-raise-frame t
  "Whether to raise and focus the Emacs frame when the editor opens a file."
  :type 'boolean)

(defcustom libregnum-subscribe
  '("changed" "selection" "level" "saved" "tool" "log" "play" "run")
  "Events to receive from the editor."
  :type '(repeat string))

(defcustom libregnum-log-buffer-name "*libregnum-log*"
  "Name of the buffer the editor log is streamed into."
  :type 'string)

(defcustom libregnum-log-max-lines 5000
  "Maximum number of lines kept in the log buffer."
  :type 'integer)

(defcustom libregnum-revert-on-save t
  "Revert unmodified buffers visiting a level when the editor saves it."
  :type 'boolean)

(defcustom libregnum-keymap-prefix (kbd "C-c L")
  "Prefix key for `libregnum-command-map' in `libregnum-global-mode'."
  :type 'key-sequence)

(defface libregnum-selected-face
  '((t :inherit highlight :weight bold))
  "Face for selected nodes in the outline.")

(defface libregnum-dim-face
  '((t :inherit shadow))
  "Face for hidden nodes and secondary information.")

;;; ==========================================================================
;;; Connection
;;; ==========================================================================

(defvar libregnum--process nil
  "The connection to the editor.")

(defvar libregnum--next-id 0
  "Id of the last request sent.")

(defvar libregnum--pending (make-hash-table :test #'eql)
  "Request id -> callback (or a cons cell for synchronous waits).")

(defvar libregnum--server-info nil
  "The editor's reply to \"hello\" (an alist).")

(defvar libregnum-event-functions nil
  "Abnormal hook run with EVENT (a string) and DATA for every event.")

(defvar libregnum--selection nil
  "GUIDs of the nodes selected in the editor.")

(defun libregnum--json-encode (object)
  "Encode OBJECT (alists, vectors, strings, numbers) as one JSON line."
  (let ((json-encoding-pretty-print nil)
        (json-false :json-false)
        (json-null :null))
    (json-encode object)))

(defun libregnum--json-decode (string)
  "Decode STRING into alists with symbol keys, vectors for arrays."
  (let ((json-object-type 'alist)
        (json-array-type 'vector)
        (json-key-type 'symbol)
        (json-false nil)
        (json-null nil))
    (json-read-from-string string)))

(defun libregnum-find-socket ()
  "Return the socket of the newest editor, or nil."
  (or (and libregnum-socket (file-exists-p libregnum-socket) libregnum-socket)
      (when (file-directory-p libregnum-socket-directory)
        (car (sort (directory-files libregnum-socket-directory t "\\`editor-[0-9]+\\.sock\\'")
                   (lambda (a b)
                     (time-less-p (file-attribute-modification-time (file-attributes b))
                                  (file-attribute-modification-time (file-attributes a)))))))))

(defun libregnum-connected-p ()
  "Return non-nil when connected to an editor."
  (and libregnum--process (process-live-p libregnum--process)))

(defun libregnum--filter (process output)
  "Accumulate OUTPUT from PROCESS and dispatch complete lines."
  (let ((buffer (process-buffer process)))
    (when (buffer-live-p buffer)
      (with-current-buffer buffer
        (goto-char (point-max))
        (insert output)
        (goto-char (point-min))
        (let (line)
          (while (search-forward "\n" nil t)
            (setq line (buffer-substring (point-min) (1- (point))))
            (delete-region (point-min) (point))
            (unless (string-empty-p line)
              (condition-case err
                  (libregnum--dispatch (libregnum--json-decode line))
                (error (message "libregnum: bad message: %s" (error-message-string err)))))
            (goto-char (point-min))))))))

(defun libregnum--dispatch (message)
  "Route MESSAGE (a decoded alist) to its callback or event handler."
  (let ((id (alist-get 'id message))
        (event (alist-get 'event message)))
    (cond
     (id
      (let ((callback (gethash id libregnum--pending)))
        (remhash id libregnum--pending)
        (cond
         ((consp callback) (setcar callback message))
         ((functionp callback) (funcall callback message)))))
     (event
      (libregnum--handle-event event (alist-get 'data message))))))

(defun libregnum--sentinel (process event)
  "Report the end of PROCESS (EVENT is the reason)."
  (unless (process-live-p process)
    (when (eq process libregnum--process)
      (setq libregnum--process nil
            libregnum--server-info nil))
    (message "libregnum: disconnected (%s)" (string-trim event))))

(defun libregnum-send (method &optional params callback)
  "Send METHOD with PARAMS (an alist); call CALLBACK with the reply alist.
Returns the request id."
  (unless (libregnum-connected-p)
    (user-error "Not connected to a libregnum editor (M-x libregnum-connect)"))
  (let ((id (cl-incf libregnum--next-id)))
    (when callback
      (puthash id callback libregnum--pending))
    (process-send-string libregnum--process
                         (concat (libregnum--json-encode
                                  `((id . ,id) (method . ,method)
                                    (params . ,(or params (make-hash-table)))))
                                 "\n"))
    id))

(defun libregnum-request (method &optional params)
  "Call METHOD with PARAMS synchronously and return its result.
Signals an error when the editor replies with one."
  (let* ((box (list nil))
         (id (libregnum-send method params box))
         (deadline (+ (float-time) libregnum-request-timeout)))
    (while (and (null (car box)) (< (float-time) deadline) (libregnum-connected-p))
      (accept-process-output libregnum--process 0.05))
    (remhash id libregnum--pending)
    (let ((reply (car box)))
      (cond
       ((null reply) (error "libregnum: no reply to %s" method))
       ((alist-get 'error reply)
        (error "libregnum: %s" (alist-get 'message (alist-get 'error reply))))
       (t (alist-get 'result reply))))))

;;;###autoload
(defun libregnum-connect (&optional socket)
  "Connect to the editor listening on SOCKET (default: the newest one)."
  (interactive
   (list (when current-prefix-arg
           (read-file-name "Editor socket: " libregnum-socket-directory nil t))))
  (let ((path (or socket (libregnum-find-socket))))
    (unless path
      (user-error "No libregnum editor found in %s (start lrgeditor first)" libregnum-socket-directory))
    (libregnum-disconnect)
    (setq libregnum--process
          (make-network-process :name "libregnum"
                                :family 'local
                                :service path
                                :buffer (generate-new-buffer " *libregnum-connection*")
                                :coding 'utf-8
                                :noquery t
                                :filter #'libregnum--filter
                                :sentinel #'libregnum--sentinel))
    (setq libregnum--server-info
          (libregnum-request "hello"
                             `((client . "emacs")
                               (capabilities . ["open-file"])
                               (subscribe . ,(vconcat libregnum-subscribe)))))
    (message "libregnum: connected to %s (project %s)" path
             (or (alist-get 'project libregnum--server-info) "none"))
    libregnum--server-info))

(defun libregnum-disconnect ()
  "Close the connection to the editor."
  (interactive)
  (when libregnum--process
    (let ((buffer (process-buffer libregnum--process)))
      (delete-process libregnum--process)
      (when (buffer-live-p buffer)
        (kill-buffer buffer))))
  (setq libregnum--process nil
        libregnum--server-info nil)
  (clrhash libregnum--pending))

(defun libregnum--ensure-connected ()
  "Connect to the newest editor unless already connected."
  (unless (libregnum-connected-p)
    (libregnum-connect)))

;;;###autoload
(defun libregnum-start-editor (target &optional headless)
  "Start `lrgeditor' on TARGET (a project folder or level) and connect.
With a prefix argument (HEADLESS), run it without a window."
  (interactive
   (list (read-file-name "Project folder or level: " nil default-directory t)
         current-prefix-arg))
  (let* ((socket (expand-file-name (format "editor-emacs-%d.sock" (emacs-pid)) libregnum-socket-directory))
         (args (append libregnum-editor-arguments
                       (list "--socket" socket)
                       (when headless (list "--headless"))
                       (list (expand-file-name target))))
         (proc (apply #'start-process "lrgeditor" (get-buffer-create "*lrgeditor*")
                      libregnum-editor-program args))
         (deadline (+ (float-time) 10.0)))
    (set-process-query-on-exit-flag proc nil)
    (while (and (not (file-exists-p socket)) (process-live-p proc) (< (float-time) deadline))
      (accept-process-output proc 0.1))
    (unless (file-exists-p socket)
      (pop-to-buffer "*lrgeditor*")
      (error "libregnum: the editor did not start (see *lrgeditor*)"))
    (libregnum-connect socket)))

(defun libregnum-status ()
  "Describe the connected editor."
  (interactive)
  (if (not (libregnum-connected-p))
      (message "libregnum: not connected")
    (let ((info (libregnum-request "editor.info")))
      (message "libregnum: %s%s, %d nodes, %d selected, tool %s%s"
               (or (alist-get 'level info) "untitled")
               (if (alist-get 'dirty info) " (modified)" "")
               (or (alist-get 'nodes info) 0)
               (length (alist-get 'nodes (alist-get 'selection info)))
               (alist-get 'tool info)
               (if (eq (alist-get 'clients info) 1) "" (format ", %s clients" (alist-get 'clients info)))))))

;;; ==========================================================================
;;; Events
;;; ==========================================================================

(defvar libregnum--refresh-timer nil
  "Pending outline refresh.")

(defun libregnum-default-open-file (file line column)
  "Visit FILE at LINE and COLUMN, raising the frame if configured."
  (let ((buffer (find-file-noselect file)))
    (pop-to-buffer buffer)
    (when (and line (> line 0))
      (goto-char (point-min))
      (forward-line (1- line))
      (when (and column (> column 0))
        (move-to-column (1- column))))
    (when libregnum-raise-frame
      (select-frame-set-input-focus (selected-frame)))))

(defun libregnum--handle-event (event data)
  "Act on EVENT (a string) with DATA from the editor."
  (pcase event
    ("open-file"
     (funcall libregnum-open-file-function
              (alist-get 'file data) (alist-get 'line data) (alist-get 'col data)))
    ("log" (libregnum--log-append data))
    ("selection"
     (setq libregnum--selection (append (alist-get 'nodes data) nil))
     (libregnum--schedule-refresh))
    ((or "changed" "level") (libregnum--schedule-refresh))
    ("saved"
     (when libregnum-revert-on-save
       (let ((buffer (and (alist-get 'path data) (get-file-buffer (alist-get 'path data)))))
         (when (and buffer (not (buffer-modified-p buffer)))
           (with-current-buffer buffer
             (revert-buffer t t t)))))))
  (run-hook-with-args 'libregnum-event-functions event data))

(defun libregnum--schedule-refresh ()
  "Refresh visible libregnum buffers soon (events come in bursts)."
  (when libregnum--refresh-timer
    (cancel-timer libregnum--refresh-timer))
  (setq libregnum--refresh-timer
        (run-with-idle-timer 0.15 nil #'libregnum--refresh-buffers)))

(defun libregnum--refresh-buffers ()
  "Refresh every live outline buffer."
  (setq libregnum--refresh-timer nil)
  (dolist (buffer (buffer-list))
    (with-current-buffer buffer
      (when (and (derived-mode-p 'libregnum-outline-mode) (libregnum-connected-p))
        (ignore-errors (libregnum-outline-refresh))))))

;;; ==========================================================================
;;; Log
;;; ==========================================================================

(defun libregnum--log-append (record)
  "Append the log RECORD (an alist) to the log buffer."
  (let ((buffer (get-buffer-create libregnum-log-buffer-name)))
    (with-current-buffer buffer
      (unless (derived-mode-p 'libregnum-log-mode)
        (libregnum-log-mode))
      (let ((inhibit-read-only t)
            (follow (= (point) (point-max)))
            (level (or (alist-get 'level record) "message")))
        (save-excursion
          (goto-char (point-max))
          (insert (propertize (format "%-8s" level)
                              'face (pcase level
                                      ((or "error" "critical") 'error)
                                      ("warning" 'warning)
                                      (_ 'libregnum-dim-face)))
                  (format "%-10s " (or (alist-get 'domain record) ""))
                  (or (alist-get 'message record) "")
                  "\n")
          (when (> (count-lines (point-min) (point-max)) libregnum-log-max-lines)
            (goto-char (point-min))
            (forward-line (/ libregnum-log-max-lines 10))
            (delete-region (point-min) (point))))
        (when follow
          (goto-char (point-max)))))))

(define-derived-mode libregnum-log-mode special-mode "LRG-Log"
  "Major mode for the libregnum editor log.")

(defun libregnum-log ()
  "Show the editor log, filled with recent history."
  (interactive)
  (let ((buffer (get-buffer-create libregnum-log-buffer-name)))
    (with-current-buffer buffer
      (unless (derived-mode-p 'libregnum-log-mode)
        (libregnum-log-mode))
      (when (and (libregnum-connected-p) (= (buffer-size) 0))
        (seq-do #'libregnum--log-append (libregnum-request "log.tail" '((count . 200))))))
    (pop-to-buffer buffer)))

;;; ==========================================================================
;;; Nodes
;;; ==========================================================================

(defun libregnum--flatten-tree (nodes depth)
  "Flatten the node.tree result NODES into (DEPTH . NODE) pairs."
  (let (out)
    (seq-doseq (node nodes)
      (push (cons depth node) out)
      (setq out (append (reverse (libregnum--flatten-tree (alist-get 'nodes node) (1+ depth))) out)))
    (nreverse out)))

(defun libregnum--node-candidates ()
  "Return an alist of (\"Path/To/Node\" . GUID) for every node."
  (let (out)
    (cl-labels ((walk (nodes prefix)
                  (seq-doseq (node nodes)
                    (let ((path (if prefix (concat prefix "/" (alist-get 'name node)) (alist-get 'name node))))
                      (push (cons path (alist-get 'guid node)) out)
                      (walk (alist-get 'nodes node) path)))))
      (walk (libregnum-request "node.tree") nil))
    (nreverse out)))

(defun libregnum-read-node (&optional prompt)
  "Read a node with completion (PROMPT); default to the editor's selection."
  (libregnum--ensure-connected)
  (let* ((candidates (libregnum--node-candidates))
         (default (car (rassoc (car (last libregnum--selection)) candidates)))
         (choice (completing-read (or prompt "Node: ") candidates nil t nil nil default)))
    (or (cdr (assoc choice candidates)) choice)))

(defun libregnum--node-at-point-or-read (prompt)
  "The node on this outline line, or one read with PROMPT."
  (or (get-text-property (line-beginning-position) 'libregnum-guid)
      (libregnum-read-node prompt)))

(defun libregnum-select-node (guid &optional additive)
  "Select the node GUID in the editor (ADDITIVE adds to the selection)."
  (interactive (list (libregnum-read-node "Select: ") current-prefix-arg))
  (libregnum-request "selection.set" `((nodes . [,guid]) (additive . ,(if additive t :json-false)))))

(defun libregnum-rename-node (guid name)
  "Rename the node GUID to NAME."
  (interactive
   (let ((guid (libregnum--node-at-point-or-read "Rename: ")))
     (list guid (read-string "New name: " (alist-get 'name (libregnum-request "node.get" `((node . ,guid))))))))
  (libregnum-request "node.rename" `((node . ,guid) (name . ,name))))

(defun libregnum-delete-node (guid)
  "Delete the node GUID (undoable in the editor)."
  (interactive (list (libregnum--node-at-point-or-read "Delete: ")))
  (libregnum-request "node.delete" `((node . ,guid))))

(defun libregnum-duplicate-node (guid)
  "Duplicate the node GUID."
  (interactive (list (libregnum--node-at-point-or-read "Duplicate: ")))
  (libregnum-request "node.duplicate" `((node . ,guid))))

(defconst libregnum--kinds
  '("none" "primitive" "mesh-asset" "sprite" "tilemap" "light" "camera" "audio-emitter")
  "Node visual kinds accepted by node.create.")

(defconst libregnum--primitives
  '("cube" "uv-sphere" "plane" "cylinder" "cone" "torus" "ico-sphere" "circle" "grid"
    "rectangle-2d" "circle-2d")
  "Primitive shapes accepted by node.create.")

(defun libregnum-create-node (name kind &optional primitive parent)
  "Create a node NAME of KIND (with PRIMITIVE) under PARENT (a GUID)."
  (interactive
   (let* ((kind (completing-read "Kind: " libregnum--kinds nil t nil nil "primitive"))
          (primitive (when (string= kind "primitive")
                       (completing-read "Shape: " libregnum--primitives nil t nil nil "cube")))
          (name (read-string "Name: " (capitalize (or primitive kind)))))
     (list name kind primitive (get-text-property (line-beginning-position) 'libregnum-guid))))
  (libregnum-request "node.create"
                     `((name . ,name) (kind . ,kind)
                       ,@(when primitive `((primitive . ,primitive)))
                       ,@(when parent `((parent . ,parent))))))

(defun libregnum--read-vector (prompt current)
  "Read three numbers with PROMPT, defaulting to CURRENT (a vector)."
  (let ((text (read-string prompt (mapconcat (lambda (v) (format "%g" v)) current " "))))
    (vconcat (mapcar #'string-to-number (split-string text "[ ,]+" t)))))

(defun libregnum-set-property (guid property)
  "Edit PROPERTY of the node GUID.
PROPERTY is name, location, rotation_degrees, scale, visible or locked."
  (interactive
   (list (libregnum--node-at-point-or-read "Node: ")
         (completing-read "Property: " '("location" "rotation_degrees" "scale" "name" "visible" "locked") nil t)))
  (let* ((node (libregnum-request "node.get" `((node . ,guid))))
         (value
          (pcase property
            ("name" (read-string "Name: " (alist-get 'name node)))
            ((or "visible" "locked") (if (y-or-n-p (format "%s? " (capitalize property))) t :json-false))
            ("rotation_degrees"
             (libregnum--read-vector "Rotation (degrees x y z): "
                                     (seq-map (lambda (r) (/ (* r 180.0) float-pi)) (alist-get 'rotation node))))
            (_ (libregnum--read-vector (format "%s (x y z): " (capitalize property))
                                       (alist-get (intern property) node))))))
    (libregnum-request "node.set" `((node . ,guid) (,(intern property) . ,value)))))

(defun libregnum-open-scripts (guid)
  "Open the scripts attached to the node GUID (in this Emacs)."
  (interactive (list (libregnum--node-at-point-or-read "Scripts of: ")))
  (let ((scripts (alist-get 'scripts (libregnum-request "node.get" `((node . ,guid))))))
    (if (= (length scripts) 0)
        (message "libregnum: no scripts on that node")
      (dotimes (i (length scripts))
        (libregnum-request "script.open" `((node . ,guid) (index . ,i)))))))

;;; ==========================================================================
;;; Inspect
;;; ==========================================================================

(define-derived-mode libregnum-inspect-mode special-mode "LRG-Inspect"
  "Major mode showing one node of the libregnum editor."
  (setq-local revert-buffer-function #'libregnum--inspect-revert))

(defvar-local libregnum--inspect-guid nil
  "GUID of the node shown in this inspect buffer.")

(defun libregnum--inspect-revert (&rest _)
  "Redisplay the inspected node."
  (libregnum-inspect libregnum--inspect-guid))

(defun libregnum-inspect (guid)
  "Show everything about the node GUID."
  (interactive (list (libregnum--node-at-point-or-read "Inspect: ")))
  (let ((node (libregnum-request "node.get" `((node . ,guid))))
        (buffer (get-buffer-create "*libregnum-inspect*")))
    (with-current-buffer buffer
      (libregnum-inspect-mode)
      (setq libregnum--inspect-guid guid)
      (let ((inhibit-read-only t))
        (erase-buffer)
        (insert (propertize (or (alist-get 'name node) "(unnamed)") 'face 'bold)
                "  " (propertize (or (alist-get 'path node) "") 'face 'libregnum-dim-face) "\n\n")
        (pcase-dolist (`(,key . ,value) node)
          (unless (memq key '(name path children))
            (insert (propertize (format "%-11s" key) 'face 'font-lock-keyword-face)
                    (string-trim (pp-to-string value)) "\n")))
        (goto-char (point-min))))
    (display-buffer buffer)))

;;; ==========================================================================
;;; Outline
;;; ==========================================================================

(defvar libregnum-outline-mode-map
  (let ((map (make-sparse-keymap)))
    (set-keymap-parent map special-mode-map)
    (define-key map (kbd "RET") #'libregnum-outline-select)
    (define-key map (kbd "<return>") #'libregnum-outline-select)
    (define-key map (kbd "TAB") #'libregnum-outline-inspect)
    (define-key map "g" #'libregnum-outline-refresh)
    (define-key map "i" #'libregnum-outline-inspect)
    (define-key map "r" #'libregnum-rename-node)
    (define-key map "e" #'libregnum-set-property)
    (define-key map "c" #'libregnum-create-node)
    (define-key map "D" #'libregnum-delete-node)
    (define-key map "y" #'libregnum-duplicate-node)
    (define-key map "o" #'libregnum-open-scripts)
    (define-key map "u" #'libregnum-undo)
    (define-key map (kbd "C-r") #'libregnum-redo)
    (define-key map "s" #'libregnum-save)
    (define-key map "n" #'next-line)
    (define-key map "p" #'previous-line)
    map)
  "Keymap for `libregnum-outline-mode'.")

(define-derived-mode libregnum-outline-mode special-mode "LRG-Outline"
  "Major mode showing the node tree of the open level.

\\{libregnum-outline-mode-map}"
  (setq-local revert-buffer-function (lambda (&rest _) (libregnum-outline-refresh)))
  (setq truncate-lines t))

(with-eval-after-load 'evil
  ;; evil's normal state shadows mode maps in special buffers; bind with
  ;; the immediate variant so the keys work on first use.
  (when (fboundp 'evil-define-key*)
    (evil-define-key* 'normal libregnum-outline-mode-map
      (kbd "RET") #'libregnum-outline-select
      (kbd "<return>") #'libregnum-outline-select
      (kbd "TAB") #'libregnum-outline-inspect
      "gr" #'libregnum-outline-refresh
      "i" #'libregnum-outline-inspect
      "r" #'libregnum-rename-node
      "e" #'libregnum-set-property
      "c" #'libregnum-create-node
      "D" #'libregnum-delete-node
      "y" #'libregnum-duplicate-node
      "o" #'libregnum-open-scripts
      "u" #'libregnum-undo
      (kbd "C-r") #'libregnum-redo
      "q" #'quit-window)))

(defun libregnum-outline-refresh ()
  "Redraw the outline from the editor, keeping point on the same node."
  (interactive)
  (let* ((tree (libregnum-request "node.tree"))
         (info (libregnum-request "editor.info"))
         (selected (append (alist-get 'nodes (alist-get 'selection info)) nil))
         (current (get-text-property (line-beginning-position) 'libregnum-guid))
         (inhibit-read-only t)
         target)
    (setq libregnum--selection selected)
    (erase-buffer)
    (insert (propertize (format "%s%s" (or (alist-get 'level info) "untitled")
                                (if (alist-get 'dirty info) " [modified]" ""))
                        'face 'bold)
            (propertize (format "   %s nodes, tool: %s\n\n" (alist-get 'nodes info) (alist-get 'tool info))
                        'face 'libregnum-dim-face))
    (pcase-dolist (`(,depth . ,node) (libregnum--flatten-tree tree 0))
      (let* ((guid (alist-get 'guid node))
             (start (point))
             (kind (alist-get 'kind node)))
        (insert (make-string (* 2 depth) ?\s)
                (if (> (or (alist-get 'children node) 0) 0) "- " "  ")
                (or (alist-get 'name node) "(unnamed)")
                (propertize (format "  %s%s%s" (if (equal kind "none") "" kind)
                                    (if (alist-get 'visible node) "" " hidden")
                                    (if (alist-get 'locked node) " locked" ""))
                            'face 'libregnum-dim-face)
                "\n")
        (put-text-property start (point) 'libregnum-guid guid)
        (cond
         ((member guid selected) (add-face-text-property start (1- (point)) 'libregnum-selected-face))
         ((not (alist-get 'visible node)) (add-face-text-property start (1- (point)) 'libregnum-dim-face)))
        (when (equal guid current)
          (setq target start))))
    (goto-char (or target (point-min)))))

(defun libregnum-outline-select (&optional additive)
  "Select the node on this line in the editor (ADDITIVE with a prefix)."
  (interactive "P")
  (let ((guid (get-text-property (line-beginning-position) 'libregnum-guid)))
    (when guid
      (libregnum-select-node guid additive))))

(defun libregnum-outline-inspect ()
  "Inspect the node on this line."
  (interactive)
  (let ((guid (get-text-property (line-beginning-position) 'libregnum-guid)))
    (when guid
      (libregnum-inspect guid))))

;;;###autoload
(defun libregnum-outline ()
  "Show the level open in the editor as an outline."
  (interactive)
  (libregnum--ensure-connected)
  (let ((buffer (get-buffer-create "*libregnum-outline*")))
    (with-current-buffer buffer
      (unless (derived-mode-p 'libregnum-outline-mode)
        (libregnum-outline-mode))
      (libregnum-outline-refresh))
    (pop-to-buffer buffer)))

;;; ==========================================================================
;;; Editor commands
;;; ==========================================================================

(defun libregnum-action (id)
  "Run the editor action ID, read with completion over labels."
  (interactive
   (progn
     (libregnum--ensure-connected)
     (let* ((actions (libregnum-request "action.list"))
            (candidates
             (mapcar (lambda (a)
                       (cons (format "%s: %s%s" (alist-get 'category a) (alist-get 'label a)
                                     (if (alist-get 'shortcut a) (format "  (%s)" (alist-get 'shortcut a)) ""))
                             (alist-get 'id a)))
                     actions)))
       (list (cdr (assoc (completing-read "Editor action: " candidates nil t) candidates))))))
  (libregnum-request "action.run" `((id . ,id))))

(defun libregnum-save ()
  "Save the level in the editor."
  (interactive)
  (libregnum-request "level.save")
  (message "libregnum: saved"))

(defun libregnum-undo ()
  "Undo in the editor."
  (interactive)
  (libregnum-request "edit.undo"))

(defun libregnum-redo ()
  "Redo in the editor."
  (interactive)
  (libregnum-request "edit.redo"))

(defun libregnum-play ()
  "Start play-in-editor."
  (interactive)
  (libregnum-request "play.start"))

(defun libregnum-stop ()
  "Stop play-in-editor (the level is restored)."
  (interactive)
  (libregnum-request "play.stop"))

(defun libregnum-pause ()
  "Pause or resume play-in-editor."
  (interactive)
  (libregnum-request "play.pause"))

(defun libregnum-step ()
  "Advance play-in-editor by one frame."
  (interactive)
  (libregnum-request "play.step"))

(defun libregnum-run-game ()
  "Run the game as a separate process (output goes to the log)."
  (interactive)
  (libregnum-request "run.start")
  (message "libregnum: game started (M-x libregnum-log for output)"))

(defun libregnum-set-tool (tool)
  "Switch the editor's gizmo to TOOL."
  (interactive (list (completing-read "Tool: " '("select" "translate" "rotate" "scale") nil t)))
  (libregnum-request "tool.set" `((tool . ,tool))))

(defun libregnum-visit-level ()
  "Visit the YAML of the level open in the editor.
The editor reloads it when you save, unless it has unsaved changes."
  (interactive)
  (let ((level (alist-get 'level (libregnum-request "editor.info"))))
    (unless level
      (user-error "The level has not been saved yet"))
    (find-file level)))

(defun libregnum-screenshot (file)
  "Save the editor viewport to FILE (a .png)."
  (interactive (list (read-file-name "Screenshot to: " nil nil nil "viewport.png")))
  (libregnum-request "view.screenshot" `((path . ,(expand-file-name file)))))

(define-compilation-mode libregnum-problems-mode "LRG-Problems"
  "Mode for the editor's level problems."
  (setq-local compilation-error-regexp-alist
              '(("^\\([^:\n]+\\):\\(error\\|warning\\|info\\): " 1 nil nil (2 . 3)))))

(defun libregnum-validate ()
  "Validate the level and list the problems."
  (interactive)
  (let* ((info (libregnum-request "editor.info"))
         (level (or (alist-get 'level info) "untitled"))
         (problems (libregnum-request "level.validate"))
         (buffer (get-buffer-create "*libregnum-problems*")))
    (with-current-buffer buffer
      (let ((inhibit-read-only t))
        (erase-buffer)
        (insert (format "Problems in %s\n\n" level))
        (seq-doseq (p problems)
          (insert (format "%s:%s: %s%s%s [%s]\n"
                          (or (alist-get 'file p) level) (alist-get 'severity p)
                          (or (alist-get 'path p) "") (if (alist-get 'path p) ": " "")
                          (alist-get 'message p) (alist-get 'code p))))
        (insert (format "\n%d problem(s)\n" (length problems))))
      (libregnum-problems-mode))
    (display-buffer buffer)))

;;; ==========================================================================
;;; Level files
;;; ==========================================================================

(defvar libregnum-rlevel-font-lock-keywords
  `(("^\\s-*\\(- \\)?\\([A-Za-z_][A-Za-z0-9_]*\\):" 2 font-lock-keyword-face)
    ("\\_<\\(true\\|false\\|null\\)\\_>" . font-lock-constant-face)
    ("\\_<-?[0-9]+\\(\\.[0-9]+\\)?\\_>" . font-lock-constant-face)
    ("#.*$" . font-lock-comment-face))
  "Highlighting for libregnum level files.")

(defvar libregnum-rlevel-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map (kbd "C-c C-c") #'libregnum-rlevel-open-in-editor)
    (define-key map (kbd "C-c C-v") #'libregnum-validate)
    map)
  "Keymap for `libregnum-rlevel-mode'.")

(defun libregnum--rlevel-parent-mode ()
  "Use `yaml-mode' (or `yaml-ts-mode') as the parent when installed."
  (cond ((fboundp 'yaml-mode) 'yaml-mode)
        ;; yaml-ts-mode is always defined; without the grammar it would
        ;; prompt to install one every time a level is visited.
        ((and (fboundp 'yaml-ts-mode) (fboundp 'treesit-language-available-p)
              (treesit-language-available-p 'yaml))
         'yaml-ts-mode)
        (t 'prog-mode)))

(defalias 'libregnum--rlevel-base-mode (libregnum--rlevel-parent-mode)
  "Parent of `libregnum-rlevel-mode', chosen when the package loads.")

;;;###autoload
(define-derived-mode libregnum-rlevel-mode libregnum--rlevel-base-mode "LRG-Level"
  "Major mode for libregnum level, prefab and project files (YAML).

Saving is enough to update an editor showing the level: it reloads
files that change on disk when it has no unsaved changes of its own.

\\{libregnum-rlevel-mode-map}"
  (setq-local comment-start "# ")
  (setq-local indent-tabs-mode nil)
  (unless (memq (libregnum--rlevel-parent-mode) '(yaml-mode yaml-ts-mode))
    (setq-local font-lock-defaults '(libregnum-rlevel-font-lock-keywords))))

(defun libregnum-rlevel-open-in-editor ()
  "Open this level in the connected editor."
  (interactive)
  (libregnum--ensure-connected)
  (when (buffer-modified-p)
    (save-buffer))
  (libregnum-request "level.open" `((path . ,(buffer-file-name)))))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.\\(rlevel\\|rprefab\\|ryaml\\)\\'" . libregnum-rlevel-mode))

;;; ==========================================================================
;;; Global mode and keys
;;; ==========================================================================

(defvar libregnum-command-map
  (let ((map (make-sparse-keymap)))
    (define-key map "c" #'libregnum-connect)
    (define-key map "C" #'libregnum-start-editor)
    (define-key map "q" #'libregnum-disconnect)
    (define-key map "?" #'libregnum-status)
    (define-key map "o" #'libregnum-outline)
    (define-key map "i" #'libregnum-inspect)
    (define-key map "x" #'libregnum-action)
    (define-key map "s" #'libregnum-save)
    (define-key map "u" #'libregnum-undo)
    (define-key map "U" #'libregnum-redo)
    (define-key map "p" #'libregnum-play)
    (define-key map "P" #'libregnum-stop)
    (define-key map " " #'libregnum-pause)
    (define-key map "n" #'libregnum-step)
    (define-key map "r" #'libregnum-run-game)
    (define-key map "l" #'libregnum-log)
    (define-key map "v" #'libregnum-validate)
    (define-key map "f" #'libregnum-visit-level)
    (define-key map "t" #'libregnum-set-tool)
    (define-key map "e" #'libregnum-set-property)
    map)
  "Libregnum commands, under `libregnum-keymap-prefix'.")
(fset 'libregnum-command-map libregnum-command-map)

(defvar libregnum-global-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map libregnum-keymap-prefix 'libregnum-command-map)
    map)
  "Keymap of `libregnum-global-mode'.")

;;;###autoload
(define-minor-mode libregnum-global-mode
  "Global keys (`libregnum-keymap-prefix') for the libregnum editor."
  :global t
  :lighter (:eval (if (libregnum-connected-p) " LRG" ""))
  :keymap libregnum-global-mode-map)

(provide 'libregnum)

;;; libregnum.el ends here
