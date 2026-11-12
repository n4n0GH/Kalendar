<!--
SPDX-FileCopyrightText: 2026 Kalendar contributors
SPDX-License-Identifier: CC0-1.0
-->

# Accounts and signing in

An account is a row in the database (`Account` in `src/core/types.h`) plus a
way to sign in, which is the app's business, not the engine's: `Backend`
gives the engine a function (`Backend::authorize()`) that produces the
`Authorization` header for an account.

| Kind | `Account::Type` | Signs in with | Kept where |
| --- | --- | --- | --- |
| On this system | `Local` | nothing | |
| CalDAV | `CalDav` | user name and password (`Basic`) | KWallet |
| Google | `Google` | a token from the system (`Bearer`) | the system's Online Accounts |

Headers are remembered in memory until Kalendar quits (tokens until they
expire), so the wallet and the sign-in service are asked once, not at every
request.

## Adding an account

`AddAccountDialog.qml` calls `Backend::addCalDavAccount()` or
`addGoogleAccount()`. Both look for the account's calendars first, without
storing anything, and add the account only when that worked; the result
arrives as the signal `accountAdded()` or `accountFailed()`. Closing the
dialog while this is under way cancels it (`cancelAdding()`, which makes a
late answer be ignored).

For CalDAV the password goes to the wallet *before* the account is added,
under the id the account will have: the engine starts synchronizing the
moment an account exists, and asks for the password then.

Removing an account removes Kalendar's copy of it: the row, its calendars and
events, and the wallet entry. Nothing is sent to the server, and the system's
Online Accounts is not touched.

## Passwords: SecretStore

`SecretStore` keeps one entry per account id in the folder `Kalendar` of
KWallet's network wallet, opened asynchronously on first use. In test mode it
keeps them in memory instead, so tests never open the user's wallet.

## Google: OnlineAccounts

KDE's System Settings has *Online Accounts*, where the user signs in to
Google once for all applications. Kalendar lists those accounts and asks the
system for a token; it never sees a password, and stores nothing about the
sign-in. Signing in, signing in again and signing out are done in System
Settings (the dialog has a button that opens it).

Two things of the system are used directly rather than through their
libraries (libaccounts-qt, signon-qt), because the KDE runtime of the Flatpak
has neither:

- **Which accounts there are**: `~/.config/libaccounts-glib/accounts.db`, an
  SQLite database of libaccounts, is read (never written). `Accounts` lists
  them; `Settings` has what the sign-in needs (`CredentialsId`, and the
  `auth/…` keys with the OAuth parameters), each value written down as GVariant text, which `parseValue()` reads. Where
  an account has no `auth/…` settings of its own, they come from the
  provider's description (`google.provider`).
- **A token**: the service `com.google.code.AccountsSSO.SingleSignOn`
  (signond) on the session bus. `getAuthSessionObjectPath(credentials, "",
  "oauth2")` opens a session, and `process(parameters, "web_server")` on it
  answers with `AccessToken` and `ExpiresIn`.

The token is asked for with `UiPolicy` 2, "never show a window". If Google
wants the user to sign in again, that is reported as an error on the account
that points to System Settings, instead of a browser window opening out of
nowhere during a background synchronization.

The Flatpak needs two permissions for this, both in the manifest: reading
`xdg-config/libaccounts-glib`, and talking to the service. Inside the sandbox
the config folder is the app's own, so the path is taken from
`HOST_XDG_CONFIG_HOME`.

### Why not CalDAV for Google

The application registered with Google for KDE's Online Accounts has the
Calendar API switched on, but not the CalDAV API. Kalendar cannot change
that, so it speaks the Calendar API (see
[Synchronization](synchronization.md#googles-calendar-api)). An earlier
version signed in to Google by itself, with a client ID of the builder's own;
that is gone.

### Test mode

`OnlineAccounts(true)` asks no system: the accounts are those named in
`KALENDAR_ONLINE_ACCOUNTS` (`"1=Work,2=Home"`), and tokens are
`test-token-<id>`, which the stand-in for Google accepts.
`KALENDAR_ACCOUNTS_DATABASE` points the real reader at another database, and
`KALENDAR_GOOGLE_API` points Kalendar at another server than Google's.
