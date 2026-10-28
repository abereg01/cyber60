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
