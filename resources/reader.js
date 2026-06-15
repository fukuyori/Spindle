// Injected into every chapter page. Renders highlight marks and reports new
// selections back to C++ via the QWebChannel `spindle` object. Offsets are
// character offsets into document.body's textContent (UTF-16 units), matching
// the original Spindle highlight model.
(function () {
  if (window.__spindleReady) return;
  window.__spindleReady = true;

  var COLORS = {
    yellow: "rgba(255,215,64,0.45)",
    blue: "rgba(64,196,255,0.40)",
    pink: "rgba(255,128,171,0.40)",
    orange: "rgba(255,167,38,0.45)",
    green: "rgba(105,240,174,0.40)",
    purple: "rgba(179,136,255,0.40)"
  };

  function collectTextNodes(root) {
    var items = [];
    var walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, {
      acceptNode: function (node) {
        var p = node.parentElement;
        if (p && p.closest("mark.spindle-hl")) return NodeFilter.FILTER_REJECT;
        // Inserted translation paragraphs must NOT count toward offsets, or
        // highlight positions drift once a chapter is translated (bilingual).
        if (p && p.closest(".spindle-translation")) return NodeFilter.FILTER_REJECT;
        return NodeFilter.FILTER_ACCEPT;
      }
    });
    var offset = 0;
    var node = walker.nextNode();
    while (node) {
      var len = node.length;
      items.push({ node: node, start: offset, end: offset + len });
      offset += len;
      node = walker.nextNode();
    }
    return items;
  }

  function wrapPortion(node, ls, le, h) {
    var text = node.nodeValue || "";
    if (ls < 0 || le > text.length || ls >= le) return;
    var parent = node.parentNode;
    if (!parent) return;
    var before = text.slice(0, ls), middle = text.slice(ls, le), after = text.slice(le);
    var mark = document.createElement("mark");
    mark.className = "spindle-hl";
    mark.setAttribute("data-hl-id", h.id);
    mark.style.backgroundColor = COLORS[h.color] || COLORS.yellow;
    mark.style.color = "inherit";
    mark.style.borderRadius = "2px";
    if (h.note) mark.title = h.note;
    mark.textContent = middle;
    var frag = document.createDocumentFragment();
    if (before) frag.appendChild(document.createTextNode(before));
    frag.appendChild(mark);
    if (after) frag.appendChild(document.createTextNode(after));
    parent.replaceChild(frag, node);
  }

  function applyOne(h) {
    var items = collectTextNodes(document.body);
    var si = -1, ei = -1;
    for (var i = 0; i < items.length; i++) {
      if (si < 0 && h.start >= items[i].start && h.start < items[i].end) si = i;
      if (h.end > items[i].start && h.end <= items[i].end) ei = i;
    }
    if (si < 0 || ei < 0) return;
    for (var j = ei; j >= si; j--) {
      var it = items[j];
      var ls = j === si ? h.start - it.start : 0;
      var le = j === ei ? h.end - it.start : it.end - it.start;
      if (ls >= le) continue;
      wrapPortion(it.node, ls, le, h);
    }
  }

  function clearMarks() {
    var marks = document.querySelectorAll("mark.spindle-hl");
    for (var i = 0; i < marks.length; i++) {
      var m = marks[i];
      var parent = m.parentNode;
      if (!parent) continue;
      while (m.firstChild) parent.insertBefore(m.firstChild, m);
      parent.removeChild(m);
      if (parent.normalize) parent.normalize();
    }
  }

  function applyAll() {
    if (!window.spindle) return;
    clearMarks();
    window.spindle.currentHighlights(function (jsonStr) {
      var arr;
      try { arr = JSON.parse(jsonStr); } catch (e) { return; }
      if (!arr || !arr.length) return;
      // Apply from the last highlight backwards so earlier offsets stay valid
      // as marks (excluded from collectTextNodes) are inserted.
      arr.sort(function (a, b) { return b.start - a.start; });
      arr.forEach(applyOne);
      var marks = document.querySelectorAll("mark.spindle-hl");
      for (var k = 0; k < marks.length; k++) {
        marks[k].style.cursor = "pointer";
        marks[k].addEventListener("click", function (e) {
          e.preventDefault();
          e.stopPropagation();
          var id = this.getAttribute("data-hl-id");
          if (window.spindle && id) window.spindle.markClicked(id);
        });
      }
    });
  }

  function isNodeAfter(node, ref) {
    return (ref.compareDocumentPosition(node) & Node.DOCUMENT_POSITION_FOLLOWING) !== 0;
  }
  function isNodeBefore(node, ref) {
    return (ref.compareDocumentPosition(node) & Node.DOCUMENT_POSITION_PRECEDING) !== 0;
  }

  function boundaryToOffset(items, container, offset, isStart) {
    if (container.nodeType === Node.TEXT_NODE) {
      for (var i = 0; i < items.length; i++)
        if (items[i].node === container) return items[i].start + offset;
      return null;
    }
    var boundaryNode = container.childNodes[offset] || null;
    if (isStart) {
      for (var a = 0; a < items.length; a++) {
        var it = items[a];
        if (boundaryNode === null || boundaryNode.contains(it.node) || isNodeAfter(it.node, boundaryNode))
          return it.start;
      }
      return null;
    }
    for (var b = items.length - 1; b >= 0; b--) {
      var jt = items[b];
      if (boundaryNode === null || isNodeBefore(jt.node, boundaryNode)) return jt.end;
    }
    return null;
  }

  function onMouseUp() {
    var sel = window.getSelection();
    if (!sel || sel.isCollapsed || sel.rangeCount === 0) return;
    var range = sel.getRangeAt(0);
    var text = sel.toString();
    if (!text.trim()) return;
    if (!document.body.contains(range.startContainer) || !document.body.contains(range.endContainer)) return;
    var items = collectTextNodes(document.body);
    var start = boundaryToOffset(items, range.startContainer, range.startOffset, true);
    var end = boundaryToOffset(items, range.endContainer, range.endOffset, false);
    if (start === null || end === null || start >= end) return;
    if (window.spindle) window.spindle.selectionMade(start, end, text);
  }

  // --- translation -------------------------------------------------------
  var BLOCK_SELECTOR = "p, h1, h2, h3, h4, h5, h6, li, blockquote, figcaption, dd, dt";

  function collectLeafBlocks() {
    var nodes = document.body.querySelectorAll(BLOCK_SELECTOR);
    var out = [];
    for (var i = 0; i < nodes.length; i++) {
      var el = nodes[i];
      if (el.classList.contains("spindle-translation")) continue;
      if (el.querySelector(BLOCK_SELECTOR)) continue; // not a leaf
      var text = (el.textContent || "").trim();
      if (text.length < 2) continue;
      if (!/[\p{L}\p{N}]/u.test(text)) continue;
      out.push(el);
    }
    return out;
  }

  function ensureTranslateStyle() {
    var id = "__spindle_tr";
    var s = document.getElementById(id);
    if (!s) {
      s = document.createElement("style");
      s.id = id;
      document.documentElement.appendChild(s);
    }
    s.textContent =
      ".spindle-translation{opacity:0.85;}" +
      "body.sv-original .spindle-translation{display:none;}" +
      "body.sv-translation .spindle-source{display:none;}";
  }

  function applyViewClass(view) {
    document.body.classList.remove("sv-original", "sv-bilingual", "sv-translation");
    document.body.classList.add("sv-" + (view || "original"));
  }

  function startTranslation() {
    var blocks = collectLeafBlocks();
    window.__spindleBlocks = blocks;
    var payload = [];
    for (var i = 0; i < blocks.length; i++) {
      var next = blocks[i].nextElementSibling;
      if (next && next.classList.contains("spindle-translation")) continue; // already done
      payload.push({ index: i, text: (blocks[i].textContent || "").trim() });
    }
    if (payload.length && window.spindle) window.spindle.blocksCollected(JSON.stringify(payload));
  }

  function onTranslateView(view) {
    applyViewClass(view);
    if (view !== "original") startTranslation();
  }

  function onTranslation(index, text, state) {
    var blocks = window.__spindleBlocks;
    if (!blocks || !blocks[index]) return;
    var block = blocks[index];
    var node = block.nextElementSibling;
    if (!node || !node.classList.contains("spindle-translation")) {
      node = document.createElement("p");
      node.className = "spindle-translation";
      block.insertAdjacentElement("afterend", node);
    }
    node.textContent = text;
    if (state) node.setAttribute("data-state", state);
    else node.removeAttribute("data-state");
    if (state !== "error") block.classList.add("spindle-source");
  }

  function init() {
    new QWebChannel(qt.webChannelTransport, function (channel) {
      window.spindle = channel.objects.spindle;
      window.spindle.highlightsChanged.connect(applyAll);
      window.spindle.translateViewChanged.connect(onTranslateView);
      window.spindle.translationReady.connect(onTranslation);
      ensureTranslateStyle();
      applyAll();
      window.spindle.currentTranslateView(function (view) {
        onTranslateView(view || "original");
      });
    });
    document.addEventListener("mouseup", onMouseUp);
  }

  if (typeof QWebChannel !== "undefined" && typeof qt !== "undefined" && qt.webChannelTransport) {
    init();
  } else {
    // qwebchannel.js not ready yet; retry shortly.
    var tries = 0;
    var timer = setInterval(function () {
      if (typeof QWebChannel !== "undefined" && typeof qt !== "undefined" && qt.webChannelTransport) {
        clearInterval(timer);
        init();
      } else if (++tries > 50) {
        clearInterval(timer);
      }
    }, 20);
  }
})();
