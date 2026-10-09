// SPDX-License-Identifier: GPL-3.0-or-later
// Validate every translated HTML label, including accessible controls.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const config = JSON.parse(fs.readFileSync(path.join(root, 'web/game.json'), 'utf8'));
const app = fs.readFileSync(path.join(root, 'web/app.js'), 'utf8');
const constants = app.slice(app.indexOf('  const ASSETS'), app.indexOf('  const LANGUAGE_STORAGE_KEY'));
const translations = new Function('GAME', constants + '\nreturn TRANSLATIONS;')(config);
const html = fs.readFileSync(path.join(root, 'web/index.html'), 'utf8');
for (const match of html.matchAll(/data-i18n(?:-html|-aria-label)?="([^"]+)"/g)) {
  for (const lang of ['nl', 'en']) {
    assert.equal(typeof translations[lang][match[1]], 'string', `${lang}: missing ${match[1]}`);
  }
}
console.log('All Dutch/English text and accessible labels are defined.');
