// Fishing Valley admin: the item picker of letters (search + group tabs over the radio grid of item_picker()).
// Without this script the grid still works as plain radio buttons.
(function () {
	'use strict';
	document.querySelectorAll('[data-picker]').forEach(function (picker) {
		var q = picker.querySelector('.picker-q');
		var sel = picker.querySelector('.picker-sel b');
		var tabs = picker.querySelector('.picker-tabs');
		var cells = Array.prototype.slice.call(picker.querySelectorAll('.pick'));
		var group = '';
		function apply() {
			var s = q.value.trim().toLowerCase();
			cells.forEach(function (c) {
				var g = c.getAttribute('data-g');
				var ok = g === 'none' || ((group === '' || g === group) && (s === '' || c.getAttribute('data-s').indexOf(s) >= 0));
				c.hidden = !ok;
			});
		}
		q.addEventListener('input', apply);
		q.addEventListener('keydown', function (e) {
			if (e.key === 'Enter') {   // Enter in the search box must not send the letter
				e.preventDefault();
				var first = cells.filter(function (c) { return !c.hidden && c.getAttribute('data-g') !== 'none'; })[0];
				if (first) { first.querySelector('input').click(); }
			}
		});
		tabs.hidden = false;
		tabs.addEventListener('click', function (e) {
			var b = e.target.closest('button');
			if (!b) { return; }
			group = b.getAttribute('data-g');
			tabs.querySelectorAll('button').forEach(function (x) { x.classList.toggle('on', x === b); });
			apply();
		});
		picker.addEventListener('change', function (e) {
			if (e.target.name === 'item') {
				var n = e.target.closest('.pick').querySelector('.pick-n');
				sel.textContent = n ? n.textContent : '';
			}
		});
	});
})();
