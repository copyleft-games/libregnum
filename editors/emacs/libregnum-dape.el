;;; libregnum-dape.el --- dape configuration for the libregnum DAP server -*- lexical-binding: t; -*-

;; Copyright (C) 2026  Zach Podbielniak

;; Author: Zach Podbielniak
;; Version: 0.9.0
;; Package-Requires: ((emacs "28.1"))
;; Keywords: tools, processes
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

;; A dape configuration that attaches to the editor's Debug Adapter
;; Protocol server.  When `libregnum-connect' is up, the port is the one
;; `debug.dap_start' returns; otherwise it is `libregnum-dape-port'
;; (4711, the editor's `debug.dap-port' default).
;;
;; dape is loaded only when `libregnum-dape' runs.

;;; Code:

(declare-function libregnum-request "libregnum" (method &optional params))
(declare-function libregnum-connected-p "libregnum" ())
(declare-function dape "dape" (&optional config))

(defgroup libregnum-dape nil
  "dape configuration for the libregnum editor."
  :group 'tools
  :prefix "libregnum-dape-")

(defcustom libregnum-dape-host "127.0.0.1"
  "Host of the editor's DAP server."
  :type 'string
  :group 'libregnum-dape)

(defcustom libregnum-dape-port 4711
  "DAP port used when Emacs is not connected to the editor.
The connected path asks the editor with `debug.dap_start'."
  :type 'integer
  :group 'libregnum-dape)

(defun libregnum-dape--port ()
  "Port to attach to: the editor's, or `libregnum-dape-port'."
  (if (and (fboundp 'libregnum-connected-p)
           (libregnum-connected-p))
      (let* ((result (libregnum-request "debug.dap_start"))
             (port (and (listp result) (alist-get 'port result))))
        (or port libregnum-dape-port))
    libregnum-dape-port))

(defun libregnum-dape-config ()
  "A dape attach configuration for the editor's DAP server.
The value is a plist: modes, host, port, :request \"attach\",
:type \"libregnum\"."
  `(modes (lua-mode python-mode c-mode js-mode typescript-mode)
          host ,libregnum-dape-host
          port ,(libregnum-dape--port)
          :request "attach"
          :type "libregnum"))

;;;###autoload
(defun libregnum-dape ()
  "Attach dape to the libregnum editor's DAP server."
  (interactive)
  (unless (require 'dape nil t)
    (user-error "dape is not installed"))
  (dape (libregnum-dape-config)))

(provide 'libregnum-dape)
;;; libregnum-dape.el ends here
