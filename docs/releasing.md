<!--
SPDX-FileCopyrightText: 2026 Kalendar contributors
SPDX-License-Identifier: CC0-1.0
-->

# Releasing

Kalendar is distributed as a Flatpak from its own repository on GitHub Pages.
Publishing a release on GitHub does everything; this page says what to do
before that, what happens after, and what the set-up consists of.

## Making a release

1. **Set the version** in `CMakeLists.txt` (`project(kalendar VERSION x.y.z …)`).
   The About page and the Flatpak take it from there.
2. **Describe it** in `io.github.n4n0gh.kalendar.metainfo.xml`: add a `<release>`
   with the version, today's date and a short description at the top of
   `<releases>`. Discover shows this to users as what is new.
3. **Check** that it builds and the tests pass (`ctest --test-dir build`; the
   `appstreamtest` validates the metainfo file).
4. **Commit and push** to `main`.
5. **Publish a release** on GitHub for a new tag named `v` plus the version:

   ```sh
   gh release create v0.1.1 --target main --title "Kalendar 0.1.1" --notes "…"
   ```

   or with *Draft a new release* on the web site. The tag has to start with
   `v`: only such tags may publish to GitHub Pages (see below).

That is all. A few minutes later the new version is offered to everybody who
has Kalendar installed.

## What happens then

Publishing the release starts `.github/workflows/flatpak.yml`:

| Step | What it does |
| --- | --- |
| Build | `flatpak-builder` builds the manifest in a container with the KDE runtime, on the Flatpak branch `stable`. Everything Kalendar builds on is part of that runtime, so Kalendar is all that is built. |
| Prepare | `flatpak/make-site.sh` signs everything, writes the repository's summary, the files to install from and a small page, and builds `kalendar.flatpak`. |
| Attach | `kalendar.flatpak` is attached to the release. |
| Publish | The repository is deployed to GitHub Pages, replacing the previous one. |

It takes a few minutes.

Each publication contains the new version alone, not the history. That is all
an update needs, and it keeps the site small.

### Checking that it worked

```sh
gh run list --workflow flatpak.yml --limit 1                 # the run succeeded
gh release view v0.1.1 --json assets -q '.assets[].name'     # kalendar.flatpak is attached
flatpak remote-ls --user --updates | grep kalendar             # an installed copy sees the update
```

## What users have

- **Installing**: `flatpak install --user https://n4n0gh.github.io/Kalendar/kalendar.flatpakref`,
  the *Install Kalendar* link on <https://n4n0gh.github.io/Kalendar/>, or the
  `kalendar.flatpak` of a release.
- All three add the repository as a remote (`kalendar-origin`) with signature
  checking, so Kalendar is updated with the user's other applications, in
  Discover or with `flatpak update`.
- The published site: `repo/` (the Flatpak repository), `kalendar.flatpakref`
  (install the application), `kalendar.flatpakrepo` (add the repository only)
  and `index.html`.

## Building without releasing

The workflow can be started by hand: *Actions → Flatpak → Run workflow*, or

```sh
gh workflow run flatpak.yml --ref main                   # build only
gh workflow run flatpak.yml --ref main -f publish=true   # build and publish
```

Without `publish` the bundle is an artifact of the run (`kalendar-flatpak`) and
nothing users see changes. **With `publish`, whatever is on that branch goes
out to every installed copy as an update**, release or not. It is meant for
republishing after a fix to the workflow or the publishing script.

To try the publishing script on a locally built repository:

```sh
flatpak-builder --user --force-clean --repo=repo build-flatpak flatpak/io.github.n4n0gh.kalendar.yml
flatpak/make-site.sh repo site http://127.0.0.1:8000 master
python3 -m http.server 8000 --bind 127.0.0.1 --directory site
flatpak remote-add --user --no-gpg-verify kalendar-test http://127.0.0.1:8000/repo/
flatpak remote-ls --user kalendar-test
```

(`master` is the branch a plain `flatpak-builder` run uses; the workflow
builds `stable`.)

## The set-up

Things that exist outside the source tree and would have to be recreated for
a new home of the repository.

### GitHub Pages

- Pages is enabled for the repository with *GitHub Actions* as its source.
- The `github-pages` environment allows deployments from the branch `main`
  and from tags matching `v*`. A release runs the workflow for its tag, and
  without that rule the deployment is refused.
- The address is `https://n4n0gh.github.io/Kalendar`. It is in
  `.github/workflows/flatpak.yml` (`SITE_URL`), in the README, and **in every
  installed copy**, which updates from it. Changing it strands existing
  installations; they would have to install again from the new address.

### The signing key

Everything published is signed with a GPG key made for this purpose
(`Kalendar Flatpak repository`).

- Its secret half is the repository secret `FLATPAK_GPG_PRIVATE_KEY`:
  ASCII-armoured, without a passphrase.
- Its public half is in `kalendar.flatpakref`, `kalendar.flatpakrepo` and the
  bundle, and from there in every installed copy.
- GitHub does not give secrets back. **Whoever maintains the releases has to
  keep a copy of the secret key.** If it is lost, a new key has to be made,
  and every user has to remove the remote and install again, because their
  copies only trust the old one.
- To put a key in place: `gh secret set FLATPAK_GPG_PRIVATE_KEY < secret-key.asc`.

Without the secret the workflow still runs, and publishes an unsigned
repository. Clients that already check signatures then refuse the update, so
that is only an option for a repository nobody has installed from yet.

`make-site.sh` signs the application, its translations (`.Locale`) and debug
symbols (`.Debug`), and makes the description for software centres be written
anew so that it is signed as well. A part that is left unsigned is refused by
clients with "GPG verification enabled, but no signatures found".

## When something changes

- **A newer KDE runtime**: change `runtime-version` in the manifest and the
  container image in the workflow together (`kde-6.11` and so on). Users get
  the new runtime with the update.
- **A broken release**: fix it and publish a new version. To take a release
  back in the meantime, publish the previous one again from its tag:
  `gh workflow run flatpak.yml --ref v0.1.0 -f publish=true`. Installed
  copies follow whatever is published. Only `main` and `v*` tags may publish,
  as the set-up below says.
- **Replacing the file of a release**:
  `gh release upload v0.1.1 kalendar.flatpak --clobber`.

## Setting it up for the first time

None of the above exists until this has been done once.

1. Create the repository `Kalendar` on GitHub and push `main` to it.
2. In its settings, under *Pages*, choose *GitHub Actions* as the source.
3. Under *Environments → github-pages → Deployment branches and tags*, add a
   rule for tags matching `v*` next to the one for `main`.
4. Make a signing key and hand its secret half to the repository:

   ```sh
   export GNUPGHOME=$(mktemp -d)
   gpg --batch --passphrase "" --quick-generate-key "Kalendar Flatpak repository" ed25519 sign never
   gpg --armor --export-secret-keys > kalendar-flatpak-secret-key.asc
   gh secret set FLATPAK_GPG_PRIVATE_KEY < kalendar-flatpak-secret-key.asc
   ```

   Keep `kalendar-flatpak-secret-key.asc` somewhere safe, and out of the
   repository.
5. Publish the first release as described at the top.

## What the Flatpak cannot do

The plugin for the calendar of Plasma's clock belongs to the system's Plasma
and cannot come in a Flatpak. Installed from the source tree, it also finds
the events of a Kalendar that is installed as a Flatpak:

```sh
cmake -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --target kalendarevents
sudo cmake --install build --component plasmaplugin
```
