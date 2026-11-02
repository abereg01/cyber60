# To do: Spotify setup

After pushing `pi/` and `docs/` to GitHub, two one-time steps so the desk
screen can log in to Spotify (the QR code on the screen opens the login on
your phone; see "Spotify" in `README.md`).

- [ ] **Turn on GitHub Pages**
  1. github.com/abereg01/cyber60 → **Settings** → **Pages**.
  2. *Source*: **Deploy from a branch**. *Branch*: **main**, folder **/docs** → **Save**.
  3. After a minute or two, https://abereg01.github.io/cyber60/spotify/ shows
     "Desk screen login". That page is the last step of the phone login.

- [ ] **Create the Spotify developer app**
  1. https://developer.spotify.com/dashboard → log in → **Create app**.
  2. *App name*: Desk screen. *App description*: anything.
  3. *Redirect URI*: exactly `https://abereg01.github.io/cyber60/spotify/`
     (with the `/` at the end) → **Add**.
  4. *Which API/SDKs*: tick **Web API**. Accept the terms → **Save**.
  5. **Settings** → copy the **Client ID** and give it to Claude. It isn't a
     secret (the login uses PKCE, no client secret), so it can go in the repo.

Player controls need Spotify Premium (you have it).

# Before the Pi goes to work (~mid-December 2026): remove the build-phase SSH

Added 2026-11-02 so Claude can reach the Pi from OMV while it sits at home
(root, OMV's key only, no password).

- [ ] Put `ssh=0` in `deck.conf` and power-cycle (takes the key off the Pi).
- [ ] Delete `pi/image/files/deck-ssh.sh`, `deck-ssh.service`, `ssh/`, and
      their lines in `pi/image/build-app.sh` and `pi/image/build-image.sh`
      (marked "BUILD PHASE ONLY"), and `ssh=` in `files/deck.conf`.
- [ ] On OMV: `Host deck` in `~/.ssh/config`, `~/.ssh/id_ed25519_deck*`,
      `~/.ssh/known_hosts_deck`.
- [ ] Build the final SD image (Actions → Pi screen → Run workflow → image).
