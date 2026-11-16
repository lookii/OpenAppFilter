function showCommonModal(message, duration = 2000, type = 'info') {
    const existingModal = document.querySelector('.common-modal-mask');
    if (existingModal) {
        existingModal.remove();
    }

    const modalMask = document.createElement('div');
    modalMask.className = 'common-modal-mask';
    
    const modalContent = document.createElement('div');
    modalContent.className = 'common-modal-content';
    
    modalContent.style.backgroundColor = 'rgba(0, 0, 0, 0.5)';
    modalContent.style.color = 'white';
    
    modalContent.textContent = message;
    
    modalMask.appendChild(modalContent);
    document.body.appendChild(modalMask);
    
    setTimeout(() => {
        modalMask.classList.add('show');
    }, 10);
    
    setTimeout(() => {
        modalMask.classList.remove('show');
        setTimeout(() => {
            if (modalMask.parentNode) {
                modalMask.remove();
            }
        }, 300);
    }, duration);
}

function showSuccess(message, duration = 2000) {
    showCommonModal(message, duration);
}

function showWarning(message, duration = 2000) {
    showCommonModal(message, duration);
}

function showError(message, duration = 3000) {
    showCommonModal(message, duration);
}

function showInfo(message, duration = 2000) {
    showCommonModal(message, duration);
}

function showConfirmModal(message, onConfirm, onCancel, confirmText = '确定', cancelText = '取消') {
    const existingModal = document.querySelector('.common-modal-mask');
    if (existingModal) {
        existingModal.remove();
    }

    const modalMask = document.createElement('div');
    modalMask.className = 'common-modal-mask';
    
    const modalContent = document.createElement('div');
    modalContent.className = 'common-modal-content';
    modalContent.style.width = '300px';
    modalContent.style.height = 'auto';
    modalContent.style.minHeight = '120px';
    modalContent.style.padding = '20px';
    modalContent.style.flexDirection = 'column';
    modalContent.style.justifyContent = 'space-between';
    modalContent.style.backgroundColor = 'rgba(0, 0, 0, 0.5)';
    modalContent.style.color = 'white';
    
    const messageDiv = document.createElement('div');
    messageDiv.textContent = message;
    messageDiv.style.marginBottom = '20px';
    messageDiv.style.textAlign = 'center';
    
    const buttonContainer = document.createElement('div');
    buttonContainer.style.display = 'flex';
    buttonContainer.style.justifyContent = 'center';
    buttonContainer.style.gap = '10px';
    
    const confirmBtn = document.createElement('button');
    confirmBtn.textContent = confirmText;
    confirmBtn.style.padding = '8px 16px';
    confirmBtn.style.backgroundColor = '#2885e8';
    confirmBtn.style.color = 'white';
    confirmBtn.style.border = 'none';
    confirmBtn.style.borderRadius = '4px';
    confirmBtn.style.cursor = 'pointer';
    confirmBtn.onclick = () => {
        modalMask.remove();
        if (onConfirm) onConfirm();
    };
    
    const cancelBtn = document.createElement('button');
    cancelBtn.textContent = cancelText;
    cancelBtn.style.padding = '8px 16px';
    cancelBtn.style.backgroundColor = '#666';
    cancelBtn.style.color = 'white';
    cancelBtn.style.border = 'none';
    cancelBtn.style.borderRadius = '4px';
    cancelBtn.style.cursor = 'pointer';
    cancelBtn.onclick = () => {
        modalMask.remove();
        if (onCancel) onCancel();
    };
    
    buttonContainer.appendChild(confirmBtn);
    buttonContainer.appendChild(cancelBtn);
    
    modalContent.appendChild(messageDiv);
    modalContent.appendChild(buttonContainer);
    modalMask.appendChild(modalContent);
    document.body.appendChild(modalMask);
    
    setTimeout(() => {
        modalMask.classList.add('show');
    }, 10);
}

function formatFileSize(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

function debounce(func, wait) {
    let timeout;
    return function executedFunction(...args) {
        const later = () => {
            clearTimeout(timeout);
            func(...args);
        };
        clearTimeout(timeout);
        timeout = setTimeout(later, wait);
    };
}

function throttle(func, limit) {
    let inThrottle;
    return function() {
        const args = arguments;
        const context = this;
        if (!inThrottle) {
            func.apply(context, args);
            inThrottle = true;
            setTimeout(() => inThrottle = false, limit);
        }
    };
}

function getUrlParameter(name) {
    const urlParams = new URLSearchParams(window.location.search);
    return urlParams.get(name);
}

function setUrlParameter(name, value) {
    const url = new URL(window.location);
    url.searchParams.set(name, value);
    window.history.replaceState({}, '', url);
}

function deepClone(obj) {
    if (obj === null || typeof obj !== 'object') return obj;
    if (obj instanceof Date) return new Date(obj.getTime());
    if (obj instanceof Array) return obj.map(item => deepClone(item));
    if (typeof obj === 'object') {
        const clonedObj = {};
        for (const key in obj) {
            if (obj.hasOwnProperty(key)) {
                clonedObj[key] = deepClone(obj[key]);
            }
        }
        return clonedObj;
    }
}

function validateIP(ip) {
    if (!ip || ip.trim() === '') return true; 
    const ipRegex = /^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/;
    return ipRegex.test(ip.trim());
}

function validateNetmask(mask) {
    if (!mask || mask.trim() === '') return true; 
    const maskRegex = /^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/;
    if (!maskRegex.test(mask.trim())) return false;
    const parts = mask.split('.');
    const binary = parts.map(p => parseInt(p).toString(2).padStart(8, '0')).join('');
    return /^1+0*$/.test(binary);
}

window.showCommonModal = showCommonModal;
window.showSuccess = showSuccess;
window.showWarning = showWarning;
window.showError = showError;
window.showInfo = showInfo;
window.showConfirmModal = showConfirmModal;
window.formatFileSize = formatFileSize;
window.debounce = debounce;
window.throttle = throttle;
window.getUrlParameter = getUrlParameter;
window.setUrlParameter = setUrlParameter;
window.deepClone = deepClone;
window.validateIP = validateIP;
window.validateNetmask = validateNetmask; 

/* ==========================================================================
   OAF 界面改版统一逻辑（施工单 P0-1 / P0-2 / P0-4 / P0-5 / P0-6）
   所有逻辑都做特性检测，未引入本文件的页面或没有对应元素的页面直接跳过。
   表格由各页 JS 动态渲染，因此用 MutationObserver 在重渲染后重算。
   ========================================================================== */
(function () {
    'use strict';

    var TABLE_SELECTORS = [
        '#rules-list-container table.common-table',
        '.oaf-whitelist-table',
        '#online-record-table', '#history-record-table',
        '#visit_list_table', '.url-records-table',
        '#tab-url-lists table', '#tab-behavior-rules table'
    ];
    var TAB_SELECTORS = ['#tabmenu ul.tabs', '.tab-list', '.oaf-whitelist-tabs'];
    var refreshTimer = null;

    function allTables() {
        var out = [];
        TABLE_SELECTORS.forEach(function (sel) {
            Array.prototype.forEach.call(document.querySelectorAll(sel), function (t) {
                if (out.indexOf(t) === -1) out.push(t);
            });
        });
        return out;
    }

    /* P0-1：从表头取列名写到 td[data-label]，卡片形态用 ::before 显示字段名，
       不维护第二套 DOM，也不改各页面的渲染函数。 */
    function decorateTables() {
        allTables().forEach(function (table) {
            var heads = table.querySelectorAll('thead th');
            if (!heads.length) heads = table.querySelectorAll('tr.table-titles th');
            if (!heads.length) return;
            var labels = Array.prototype.map.call(heads, function (th) {
                return (th.textContent || '').replace(/\s+/g, ' ').trim();
            });
            var rows = table.querySelectorAll('tbody tr');
            if (!rows.length) rows = table.querySelectorAll('tr.tr:not(.table-titles)');
            Array.prototype.forEach.call(rows, function (row) {
                var cells = row.querySelectorAll('td');
                Array.prototype.forEach.call(cells, function (cell, i) {
                    if (!cell.hasAttribute('data-label') && labels[i]) {
                        cell.setAttribute('data-label', labels[i]);
                    }
                });
            });
        });
    }

    /* P0-6：把「周日,周一,… 00:00-23:59」折成紧凑 chip，原串保留在 title。 */
    function compactTimeChips() {
        if (!document.querySelector('#rules-list-container')) return;
        var days = [window._Sun, window._Mon, window._Tue, window._Wed,
                    window._Thu, window._Fri, window._Sat];
        if (!days[0]) return; // 翻译常量尚未就绪，等下一次刷新
        Array.prototype.forEach.call(
            document.querySelectorAll('#rules-list-container .time-rules-display'),
            function (el) {
                if (el.getAttribute('data-oaf-chip') === '1') return;
                var full = (el.textContent || '').trim();
                if (!full || full === '-') return;
                var m = full.match(/^(.*?)\s+(\d{2}:\d{2}-\d{2}:\d{2})$/);
                if (!m) return;
                var parts = m[1].split(/[,，]/).filter(Boolean);
                var idx = parts.map(function (p) {
                    return days.findIndex(function (d) { return d === p; });
                }).filter(function (i) { return i >= 0; });
                var label;
                if (idx.length === 7) {
                    label = window.OAF_I18N_EVERYDAY || '每天';
                } else if (idx.length === 5 && idx.indexOf(0) === -1 && idx.indexOf(6) === -1) {
                    label = window.OAF_I18N_WORKDAY || '工作日';
                } else if (idx.length === 2 && idx.indexOf(0) !== -1 && idx.indexOf(6) !== -1) {
                    label = window.OAF_I18N_WEEKEND || '周末';
                } else {
                    var sorted = idx.slice().sort(function (a, b) { return a - b; });
                    var contiguous = sorted.every(function (v, i) {
                        return i === 0 || v === sorted[i - 1] + 1;
                    });
                    label = (contiguous && sorted.length > 1)
                        ? days[sorted[0]] + '–' + days[sorted[sorted.length - 1]]
                        : parts.join('、');
                }
                var chip = document.createElement('span');
                chip.className = 'oaf-chip';
                chip.textContent = label + ' ' + m[2];
                chip.setAttribute('title', full);
                el.textContent = '';
                el.appendChild(chip);
                el.setAttribute('data-oaf-chip', '1');
            }
        );
    }

    /* P0-6：应用列逗号长串 -> 最多 3 个 chip + +N，完整串放 title。 */
    function compactAppChips() {
        var table = document.querySelector('#app_filter_rules_page .common-table');
        if (!table) return;
        Array.prototype.forEach.call(table.querySelectorAll('tbody tr'), function (row) {
            var cell = row.querySelectorAll('td')[3];
            if (!cell || cell.getAttribute('data-oaf-chip') === '1') return;
            var full = (cell.textContent || '').trim();
            if (!full || full.indexOf(',') === -1 && full.indexOf('，') === -1) return;
            var items = full.split(/[,，]/).map(function (s) { return s.trim(); }).filter(Boolean);
            cell.setAttribute('title', full);
            cell.textContent = '';
            items.slice(0, 3).forEach(function (name) {
                var chip = document.createElement('span');
                chip.className = 'oaf-chip';
                chip.textContent = name;
                cell.appendChild(chip);
            });
            if (items.length > 3) {
                var more = document.createElement('span');
                more.className = 'oaf-chip oaf-chip-more';
                more.textContent = '+' + (items.length - 3);
                more.setAttribute('title', full);
                cell.appendChild(more);
            }
            cell.setAttribute('data-oaf-chip', '1');
        });
    }

    /* P0-6：模式列拆「来源标记 + 用户名 + 附加标记」三段。 */
    function splitModeCells() {
        if (!document.querySelector('#rules-list-container')) return;
        Array.prototype.forEach.call(
            document.querySelectorAll('#rules-list-container table.common-table td:nth-child(2)'),
            function (cell) {
                if (cell.getAttribute('data-oaf-split') === '1') return;
                var text = (cell.textContent || '').trim();
                var sep = text.indexOf(':');
                if (sep === -1) sep = text.indexOf('：');
                if (sep === -1) return;
                var source = text.slice(0, sep).trim();
                var rest = text.slice(sep + 1).trim();
                var countMatch = rest.match(/\((\d+)\)\s*$/);
                var extra = countMatch ? countMatch[1] : '';
                var users = countMatch ? rest.slice(0, countMatch.index).trim() : rest;
                cell.setAttribute('title', cell.getAttribute('title') || text);
                cell.textContent = '';
                var src = document.createElement('span');
                src.className = 'oaf-mode-source';
                src.textContent = source;
                var userEl = document.createElement('span');
                userEl.className = 'oaf-mode-users';
                userEl.textContent = users;
                cell.appendChild(src);
                cell.appendChild(userEl);
                if (extra) {
                    var ex = document.createElement('span');
                    ex.className = 'oaf-mode-extra';
                    ex.textContent = ' ×' + extra;
                    cell.appendChild(ex);
                }
                cell.style.maxWidth = 'none';
                cell.setAttribute('data-oaf-split', '1');
            }
        );
    }

    /* P0-2：横滑容器、右侧渐隐遮罩、当前页签自动居中。 */
    function initTabScroll() {
        TAB_SELECTORS.forEach(function (sel) {
            Array.prototype.forEach.call(document.querySelectorAll(sel), function (box) {
                if (box.getAttribute('data-oaf-tab') === '1') return;
                box.setAttribute('data-oaf-tab', '1');
                var update = function () {
                    var atEnd = box.scrollLeft + box.clientWidth >= box.scrollWidth - 4;
                    box.classList.toggle('at-edge', atEnd);
                };
                box.addEventListener('scroll', update, { passive: true });
                update();
                var active = box.querySelector('.active, li.active, [aria-selected="true"]');
                if (active && box.scrollWidth > box.clientWidth + 4 &&
                    typeof active.scrollIntoView === 'function') {
                    try {
                        active.scrollIntoView({ inline: 'center', block: 'nearest' });
                    } catch (e) { /* 老内核不支持 options，忽略 */ }
                }
            });
        });
    }

    /* P0-5：隐藏容器里初始化的 ECharts 宽度为 0，切页签后必须 resize。 */
    function resizeCharts() {
        if (!window.echarts || !window.echarts.getInstanceByDom) return;
        Array.prototype.forEach.call(
            document.querySelectorAll('.tab-body [id^="chart"], .tab-body .chart, [id$="_chart"]'),
            function (el) {
                var inst = window.echarts.getInstanceByDom(el);
                if (inst) {
                    try { inst.resize(); } catch (e) { /* 图表已销毁 */ }
                }
            }
        );
    }

    function initChartResize() {
        document.addEventListener('click', function (ev) {
            var t = ev.target;
            while (t && t !== document) {
                if (t.classList && (t.classList.contains('tab-item') ||
                    t.classList.contains('oaf-whitelist-tab'))) {
                    setTimeout(resizeCharts, 120);
                    setTimeout(resizeCharts, 320);
                    break;
                }
                t = t.parentNode;
            }
        }, true);
        window.addEventListener('resize', function () {
            clearTimeout(window.__oafResizeTimer);
            window.__oafResizeTimer = setTimeout(resizeCharts, 150);
        });
    }

    /* P0-4：用户列表纯前端搜索，匹配终端名 / IP / MAC。 */
    function initUserSearch() {
        var input = document.getElementById('user-search-input');
        if (!input || input.getAttribute('data-oaf-ready') === '1') return;
        /* P1-5：用户列表已改用统一工具栏做数据层筛选，这里不再做 DOM 过滤，
           否则同一关键词会被过滤两次（数据层 + DOM 层），空态判定也会失真。 */
        if (document.getElementById('user-status-toolbar')) return;
        input.setAttribute('data-oaf-ready', '1');
        var clearBtn = document.getElementById('user-search-clear');
        var emptyEl = document.getElementById('user-search-empty');
        var timer = null;

        var apply = function () {
            var q = (input.value || '').trim().toLowerCase();
            var visibleCards = 0;
            Array.prototype.forEach.call(document.querySelectorAll('#user_status_cards .user-card'),
                function (card) {
                    var titleEl = card.querySelector('.user-card-title');
                    var hay = (card.textContent || '') + ' ' +
                        (card.getAttribute('title') || '') + ' ' +
                        (titleEl ? (titleEl.getAttribute('title') || '') : '');
                    var hit = !q || hay.toLowerCase().indexOf(q) !== -1;
                    card.style.display = hit ? '' : 'none';
                    if (hit) visibleCards++;
                });
            /* 组内全部被过滤时把整行一起隐藏，否则留下一串空白行 */
            Array.prototype.forEach.call(document.querySelectorAll('#user_status_cards .user-card-row'),
                function (row) {
                    var any = Array.prototype.some.call(row.querySelectorAll('.user-card'),
                        function (c) { return c.style.display !== 'none'; });
                    row.style.display = any ? '' : 'none';
                });
            Array.prototype.forEach.call(document.querySelectorAll('#user_status_table tr.tr'),
                function (row) {
                    if (row.querySelector('th')) return;
                    var hay = (row.textContent || '').toLowerCase();
                    row.style.display = (!q || hay.indexOf(q) !== -1) ? '' : 'none';
                });
            if (emptyEl) emptyEl.style.display = q && visibleCards === 0 ? 'block' : 'none';
            if (clearBtn) clearBtn.style.display = q ? 'inline-flex' : 'none';
        };

        input.addEventListener('input', function () {
            clearTimeout(timer);
            timer = setTimeout(apply, 200);
        });
        input.addEventListener('keydown', function (ev) {
            if (ev.key === 'Enter') { ev.preventDefault(); clearTimeout(timer); apply(); }
        });
        if (clearBtn) clearBtn.addEventListener('click', function () {
            input.value = '';
            apply();
            input.focus();
        });
        window.__oafUserSearchApply = apply;
    }

    function refresh() {
        decorateTables();
        compactTimeChips();
        compactAppChips();
        splitModeCells();
        initTabScroll();
        initUserSearch();
        if (window.__oafUserSearchApply) window.__oafUserSearchApply();
    }

    function boot() {
        refresh();
        initChartResize();
        /* 轮询重渲染后重算：只观察 childList，避免装饰属性触发递归 */
        if (window.MutationObserver) {
            var mo = new MutationObserver(function () {
                clearTimeout(refreshTimer);
                refreshTimer = setTimeout(refresh, 120);
            });
            mo.observe(document.body, { childList: true, subtree: true });
        }
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', boot);
    } else {
        boot();
    }
})();

/* ==========================================================================
   P1-5 / P1-8 / P1-9 / P2-16 公共能力
   - oafInitToolbar：列表工具栏（搜索清空、窄屏筛选抽屉、筛选计数）
   - oafCsvExport：按当前筛选导出 CSV（UTF-8 BOM，Excel 直接打开）
   - oafNormalizeDomain / oafValidDomain：域名与通配符校验
   - oafToast：轻量提示（保存成功等）
   ========================================================================== */
(function () {
    'use strict';

    var toastEl = null;
    var toastTimer = null;

    function oafToast(message, isError, duration) {
        if (!message) return;
        if (!toastEl) {
            toastEl = document.createElement('div');
            toastEl.className = 'oaf-toast';
            document.body.appendChild(toastEl);
        }
        toastEl.textContent = message;
        toastEl.className = 'oaf-toast' + (isError ? ' is-error' : '');
        /* 强制 reflow 让过渡生效 */
        void toastEl.offsetWidth;
        toastEl.classList.add('show');
        clearTimeout(toastTimer);
        toastTimer = setTimeout(function () {
            toastEl.classList.remove('show');
        }, duration || 2200);
    }

    function oafCsvExport(filename, headers, rows) {
        function esc(v) {
            var s = (v === null || v === undefined) ? '' : String(v);
            if (/[",\r\n]/.test(s)) {
                return '"' + s.replace(/"/g, '""') + '"';
            }
            return s;
        }
        var lines = [(headers || []).map(esc).join(',')];
        (rows || []).forEach(function (r) {
            lines.push((r || []).map(esc).join(','));
        });
        var blob = new Blob(['\ufeff' + lines.join('\r\n')], { type: 'text/csv;charset=utf-8' });
        var url = URL.createObjectURL(blob);
        var a = document.createElement('a');
        a.href = url;
        a.download = filename;
        a.style.display = 'none';
        document.body.appendChild(a);
        a.click();
        setTimeout(function () {
            URL.revokeObjectURL(url);
            if (a.parentNode) a.parentNode.removeChild(a);
        }, 120);
    }

    function oafNormalizeDomain(value) {
        var v = String(value === null || value === undefined ? '' : value).trim().toLowerCase();
        v = v.replace(/^[a-z]+:\/\//, '');
        v = v.replace(/[/?#].*$/, '');
        v = v.replace(/:\d+$/, '');
        v = v.replace(/\.+$/, '');
        return v;
    }

    function oafValidDomain(value) {
        var v = oafNormalizeDomain(value);
        if (!v || v.length > 253) return false;
        var re = /^(\*\.)?([a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z]{2,63}$/;
        if (!re.test(v)) return false;
        /* 通配符只允许出现在最左侧且其后至少两级域名 */
        if (v.indexOf('*') >= 0 && v.indexOf('*.') !== 0) return false;
        return true;
    }

    /* opts: { root, onChange(state), defaults:{status,time,size} } */
    function oafInitToolbar(rootSel, onChange) {
        var root = document.querySelector(rootSel);
        if (!root) return null;

        var searchWrap = root.querySelector('.oaf-toolbar-search');
        var input = root.querySelector('.oaf-toolbar-search input');
        var filtersWrap = root.querySelector('.oaf-toolbar-filters');
        var sizeWrap = root.querySelector('.oaf-toolbar-size');
        var filterBtn = root.querySelector('.oaf-toolbar-btn-filter');

        if (searchWrap && input) {
            if (!searchWrap.querySelector('.oaf-search-icon')) {
                var icon = document.createElement('span');
                icon.className = 'oaf-search-icon';
                icon.innerHTML = '<svg viewBox="0 0 16 16" fill="currentColor" aria-hidden="true">' +
                    '<path d="M11.742 10.344a6.5 6.5 0 1 0-1.397 1.398h-.001q.044.06.098.115l3.85 3.85a1 1 0 0 0 1.415-1.414l-3.85-3.85a1 1 0 0 0-.115-.098M12 6.5a5.5 5.5 0 1 1-11 0 5.5 5.5 0 0 1 11 0"/></svg>';
                searchWrap.insertBefore(icon, input);
            }
            if (!searchWrap.querySelector('.oaf-search-clear')) {
                var clear = document.createElement('button');
                clear.type = 'button';
                clear.className = 'oaf-search-clear';
                clear.setAttribute('aria-label', 'clear');
                clear.textContent = '×';
                clear.addEventListener('click', function () {
                    input.value = '';
                    searchWrap.classList.remove('has-value');
                    emit(true);
                    input.focus();
                });
                searchWrap.appendChild(clear);
            }
            input.addEventListener('input', function () {
                searchWrap.classList.toggle('has-value', !!input.value);
                emit(false);
            });
        }

        var drawer = null;
        var drawerMask = null;
        var movedItems = [];

        function state() {
            var s = { text: input ? input.value.trim() : '' };
            if (filtersWrap) {
                Array.prototype.forEach.call(filtersWrap.querySelectorAll('[data-filter]'), function (el) {
                    s[el.getAttribute('data-filter')] = el.value;
                });
                Array.prototype.forEach.call(filtersWrap.querySelectorAll('[data-filter-input]'), function (el) {
                    s[el.getAttribute('data-filter-input')] = el.value;
                });
            }
            if (sizeWrap) {
                var sel = sizeWrap.querySelector('select');
                if (sel) s.size = sel.value;
            }
            return s;
        }

        var emitTimer = null;
        function emit(immediate) {
            clearTimeout(emitTimer);
            var run = function () {
                if (typeof onChange === 'function') onChange(state());
                updateBadge();
            };
            if (immediate) run();
            else emitTimer = setTimeout(run, 220);
        }

        function updateBadge() {
            if (!filterBtn) return;
            var n = 0;
            if (filtersWrap) {
                Array.prototype.forEach.call(filtersWrap.querySelectorAll('[data-filter]'), function (el) {
                    var def = el.getAttribute('data-default');
                    if (def !== null && el.value !== def) n++;
                });
            }
            var cnt = filterBtn.querySelector('.oaf-filter-count');
            if (cnt) cnt.textContent = String(n);
            filterBtn.classList.toggle('has-active', n > 0);
        }

        function buildDrawer() {
            drawerMask = document.createElement('div');
            drawerMask.className = 'oaf-filter-drawer-mask';
            drawer = document.createElement('div');
            drawer.className = 'oaf-filter-drawer';
            document.body.appendChild(drawerMask);
            document.body.appendChild(drawer);
            drawerMask.addEventListener('click', closeDrawer);
        }

        function openDrawer() {
            if (!drawer) buildDrawer();
            drawer.innerHTML = '';
            var title = document.createElement('div');
            title.className = 'oaf-filter-drawer-title';
            title.textContent = root.getAttribute('data-filter-title') || '筛选';
            drawer.appendChild(title);

            movedItems = [];
            var sources = [];
            if (filtersWrap) {
                Array.prototype.forEach.call(filtersWrap.children, function (el) {
                    if (el.matches('select, input')) sources.push({ el: el, parent: filtersWrap });
                });
            }
            if (sizeWrap) {
                var sizeSel = sizeWrap.querySelector('select');
                if (sizeSel) sources.push({ el: sizeSel, parent: sizeWrap, label: sizeWrap.getAttribute('data-label') });
            }

            sources.forEach(function (item) {
                var row = document.createElement('div');
                row.className = 'oaf-filter-drawer-row';
                var label = document.createElement('label');
                label.textContent = item.el.getAttribute('data-label') || item.label || '';
                row.appendChild(label);
                row.appendChild(item.el);
                drawer.appendChild(row);
                movedItems.push(item);
            });

            var actions = document.createElement('div');
            actions.className = 'oaf-filter-drawer-actions';
            var reset = document.createElement('button');
            reset.type = 'button';
            reset.className = 'oaf-filter-drawer-reset';
            reset.textContent = root.getAttribute('data-reset-text') || '重置';
            reset.addEventListener('click', function () {
                movedItems.forEach(function (item) {
                    if (item.el.matches('[data-filter]')) {
                        var def = item.el.getAttribute('data-default');
                        if (def !== null) item.el.value = def;
                    } else if (item.el.matches('[data-filter-input]')) {
                        item.el.value = '';
                    } else if (item.el.tagName === 'SELECT' && item.el.options.length) {
                        item.el.selectedIndex = 0;
                    }
                    item.el.dispatchEvent(new Event('change', { bubbles: true }));
                });
                emit(true);
            });
            var apply = document.createElement('button');
            apply.type = 'button';
            apply.className = 'oaf-filter-drawer-apply';
            apply.textContent = root.getAttribute('data-apply-text') || '应用';
            apply.addEventListener('click', function () {
                emit(true);
                closeDrawer();
            });
            actions.appendChild(reset);
            actions.appendChild(apply);
            drawer.appendChild(actions);

            drawerMask.style.display = 'block';
            drawer.style.display = 'block';
            void drawer.offsetWidth;
            drawerMask.classList.add('show');
            drawer.classList.add('show');
            document.documentElement.style.overflow = 'hidden';
        }

        function closeDrawer() {
            if (!drawer) return;
            drawer.classList.remove('show');
            drawerMask.classList.remove('show');
            document.documentElement.style.overflow = '';
            setTimeout(function () {
                if (drawer.classList.contains('show')) return;
                drawer.style.display = 'none';
                drawerMask.style.display = 'none';
                /* 把控件放回原位，保持桌面端与窄屏状态一致 */
                movedItems.forEach(function (item) {
                    if (item.el.parentNode !== item.parent) item.parent.appendChild(item.el);
                });
                movedItems = [];
            }, 240);
        }

        if (filterBtn) filterBtn.addEventListener('click', openDrawer);

        if (filtersWrap) {
            Array.prototype.forEach.call(filtersWrap.querySelectorAll('[data-filter]'), function (el) {
                el.addEventListener('change', function () { emit(true); });
            });
        }
        if (sizeWrap) {
            var sizeSelect = sizeWrap.querySelector('select');
            if (sizeSelect) sizeSelect.addEventListener('change', function () { emit(true); });
        }

        updateBadge();

        return { state: state, emit: emit, closeDrawer: closeDrawer };
    }

    window.oafToast = oafToast;
    window.oafCsvExport = oafCsvExport;
    window.oafNormalizeDomain = oafNormalizeDomain;
    window.oafValidDomain = oafValidDomain;
    window.oafInitToolbar = oafInitToolbar;
})();
