// Thin fetch wrapper around the citylife server's JSON API.
window.CityAPI = (function () {
  async function call(method, url, body) {
    const res = await fetch(url, {
      method,
      credentials: 'same-origin',
      headers: body ? { 'Content-Type': 'application/json' } : undefined,
      body: body ? JSON.stringify(body) : undefined,
    });
    let data = null;
    try { data = await res.json(); } catch (e) { /* no body */ }
    if (!res.ok) {
      const err = new Error((data && data.error) || `Request failed (${res.status})`);
      err.status = res.status;
      err.data = data;
      throw err;
    }
    return data;
  }

  return {
    register: (username, password) => call('POST', '/api/register', { username, password }),
    login: (username, password) => call('POST', '/api/login', { username, password }),
    logout: () => call('POST', '/api/logout'),
    me: () => call('GET', '/api/me'),
    save: (state) => call('POST', '/api/save', state),
    robBank: () => call('POST', '/api/bank/rob'),
    leaderboard: () => call('GET', '/api/leaderboard'),
  };
})();
