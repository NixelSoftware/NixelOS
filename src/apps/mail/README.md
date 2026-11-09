# Nixel Mail

Nixel Mail is a native graphical email client with account forms, a clickable
inbox, a split message reader and a multiline compose window.

Build with `make` to include `/usr/bin/mail.bin` in the OS image, or
`make mail.bin` for the standalone application. Launch `mail.bin` from the
Nixel terminal. The 760×540 window fits the default 800×600 desktop,
including its taskbar. If window creation fails, Mail prints an error in
the launching terminal.

## Set up an account

1. Open **Account** and choose **IMAP** or **POP3**.
2. Enter the incoming server and port, SMTP server and port, username,
   password, and your email address. Password characters are masked.
3. Click **Connect & open inbox**.

Account settings and credentials stay in memory for this session. Incoming
and outgoing servers share the username and password. **Disconnect** closes
the current connection. Editing account fields also closes the connection.

## Read mail

The **Inbox** screen lists ten messages at a time, newest first. Click a
sender/subject row to open the message in the reading pane. **Older** and
**Newer** page through the inbox; **Refresh** reconnects when needed and loads
the newest messages. Use the reader's **Up** and **Down** buttons or
**Page Up / Page Down** to scroll the message.

IMAP uses read-only EXAMINE and BODY.PEEK, so reading does not mark messages
seen. POP3 never deletes messages. Message sequence numbers are session
values; refresh if another client changes the mailbox.

## Compose mail

Open **Compose**, fill in **To** and **Subject**, and type into the message
editor. Click **Send** to submit or **Discard** to clear the draft. Switching
tabs preserves the draft. Enter inserts a line break in the message editor;
Tab and Shift+Tab move between fields. Arrow keys, Home, End, Backspace and
Delete edit text. Page Up / Page Down move through a long draft.

A failed send retains the draft. If the connection fails while awaiting the
final SMTP acknowledgment, delivery is uncertain: verify delivery before
retrying. The SMTP connection replaces the receiving connection; **Refresh**
reconnects to the inbox. One recipient is supported per message. Outgoing
messages include Date, From, To, Subject and MIME headers, with a base64 plain
text body. During a network operation, click **Cancel** or press Esc to cancel.
Tabs and reader scrolling remain usable while account and compose fields are
locked to protect the in-flight request. An animated indicator and status text
show DNS, TCP, TLS, authentication and inbox progress. Failures identify the
stage (including login rejection, TLS verification and reply timeouts).

## Rendering and current limits

- Implicit TLS only: IMAP 993, POP3 995, SMTP 465 by default; ports are editable.
  Certificate and hostname verification use the browser's CA bundle and TLS
  platform. A working UTC clock and RDRAND are required. STARTTLS, plaintext
  and OAuth are not implemented. Providers may require app passwords or
  disable password authentication.
- MIME multipart bodies, folded headers, base64 and quoted-printable body
  encodings are supported. Inline HTML is preferred to plain text;
  attachment bodies are skipped.
- HTML uses the shared browser parser and native rendering: paragraphs,
  headings, emphasis, lists, line breaks, table separators, entities,
  link destinations and image alt text. Scripts, forms, embedded documents
  and remote resources are blocked. CSS layout, actual images, attachment
  handling, RFC 2047 encoded headers and non-ASCII glyph rendering are not
  implemented.
- Messages are limited to 128 KiB, drafts to 32 KiB, and the reader to 2048
  rows. Malformed or oversized protocol responses close the connection.
  Inbox summaries currently retrieve complete messages, so large messages
  can prevent a page from loading. There is no disk cache or saved setup.
- Network operations use 30-second deadlines. DNS uses the kernel resolver's
  timeout; cancelled lookups are drained in the background, so Cancel returns
  immediately. A new lookup may need to wait for that cleanup. Individual
  network waits are limited to 30 seconds; a complete operation to two minutes.

Run `make test-mail test-mail-gui`. Host tests cover protocol framing, MIME,
HTML rendering, account fields, password masking, mouse navigation, inbox
selection, consecutive window-manager click events without release events,
draft editing, and sending through a mocked transport. They do not
verify provider interoperability or actual window-manager display in QEMU.
