let selectedFolders = new Set();
let allFilesData = [];
let availableTools = [];
let currentFileContext = "";

document.addEventListener("DOMContentLoaded", () => {
    if (document.getElementById('file-tree')) {
        setupFileActions();
        if (document.querySelector('#filemanager.active')) {
            fetchFiles();
        }
    }
});

function setupFileActions() {
    const refreshBtn = document.getElementById('btn-refresh');
    if (refreshBtn) refreshBtn.addEventListener('click', fetchFiles);

    const newFolderBtn = document.getElementById('btn-new-folder');
    if (newFolderBtn) {
        newFolderBtn.addEventListener('click', () => {
            if (selectedFolders.size !== 1) {
                alert("Please select exactly one parent folder.");
                return;
            }
            const parentPath = Array.from(selectedFolders)[0];
            const folderName = prompt("Create subfolder in " + parentPath + ":");
            if (folderName) {
                let path = parentPath + "/" + folderName;
                path = path.replace("//", "/");
                fetch('/system/mkdir?path=' + encodeURIComponent(path))
                    .then(res => {
                        if (res.ok) {
                            selectedFolders.clear();
                            fetchFiles();
                        } else alert("Failed to create folder");
                    })
                    .catch(err => alert("Connection Error"));
            }
        });
    }

    // UPDATED: Upload Button opens overlay instead of direct input
    const uploadBtn = document.getElementById('btn-upload');
    if (uploadBtn) {
        uploadBtn.addEventListener('click', () => {
            if (selectedFolders.size !== 1) {
                alert("Please select exactly one folder to upload to.");
                return;
            }
            openUploadOverlay();
        });
    }

    const deleteFolderBtn = document.getElementById('btn-delete-folder');
    if (deleteFolderBtn) {
        deleteFolderBtn.addEventListener('click', () => {
            if (selectedFolders.size === 0) {
                alert("No folders selected!");
                return;
            }
            const count = selectedFolders.size;
            const confirmFn = window.showConfirm || ((msg, cb) => { if (confirm(msg)) cb(); });

            confirmFn("Delete " + count + " selected folder(s) and ALL contents?", () => {
                const promises = Array.from(selectedFolders).map(path =>
                    fetch('/system/file?name=' + encodeURIComponent(path), { method: 'DELETE' })
                );
                Promise.all(promises).then(() => {
                    selectedFolders.clear();
                    fetchFiles();
                }).catch(err => {
                    alert("Error deleting folders.");
                    fetchFiles();
                });
            });
        });
    }
}

function updateToolbar() {
    const btnNewFolder = document.getElementById('btn-new-folder');
    const btnUpload = document.getElementById('btn-upload');
    const btnDelete = document.getElementById('btn-delete-folder');

    if (!btnNewFolder || !btnUpload || !btnDelete) return;

    const count = selectedFolders.size;
    const isRootSelected = selectedFolders.has("/sdcard") || selectedFolders.has("/");

    if (count === 1) {
        btnNewFolder.classList.remove('btn-hidden');
        btnUpload.classList.remove('btn-hidden');
    } else {
        btnNewFolder.classList.add('btn-hidden');
        btnUpload.classList.add('btn-hidden');
    }

    if (count >= 1 && !isRootSelected) {
        btnDelete.classList.remove('btn-hidden');
    } else {
        btnDelete.classList.add('btn-hidden');
    }
}

function fetchFiles() {
    const treeContainer = document.getElementById('file-tree');
    if (treeContainer.innerHTML === "") {
        treeContainer.innerHTML = '<div class="loading">Loading files...</div>';
    }

    selectedFolders.clear();
    updateToolbar();

    fetch('/system/listfiles')
        .then(res => res.json())
        .then(data => {
            allFilesData = data;
            renderTree(data);
        })
        .catch(err => {
            console.error(err);
            treeContainer.innerHTML = '<div style="color:#dc2626; padding:15px; text-align:center;">Failed to load.</div>';
        });
}

// DnD Helpers
function preventDefaults(e) {
    e.preventDefault();
    e.stopPropagation();
}

function handleDrop(e) {
    const dt = e.dataTransfer;
    const files = dt.files;
    document.getElementById('overlay-file-input').files = files;
    updateFileListUI();
}

function updateFileListUI() {
    const input = document.getElementById('overlay-file-input');
    const container = document.getElementById('selected-files-list');
    container.innerHTML = '';
    if (input.files.length > 0) {
        container.innerHTML = `<p><strong>${input.files.length}</strong> files ready for upload.</p>`;
    }
}

// --- Upload Overlay Logic (NEW) ---

function openUploadOverlay() {
    const targetFolder = Array.from(selectedFolders)[0];

    let overlay = document.getElementById('upload-overlay');
    if (!overlay) {
        overlay = document.createElement('div');
        overlay.id = 'upload-overlay';
        overlay.className = 'overlay';
        document.body.appendChild(overlay);
    }

    // Updated HTML structure for Multi-file & DnD
    overlay.innerHTML = `
        <div class="modal" style="width: 450px;">
            <h3>Upload to ${escapeHtml(targetFolder)}</h3>
            
            <div class="upload-tabs">
                <button class="tab-btn active" onclick="switchUploadTab('file')">Select Files</button>
                <button class="tab-btn" onclick="switchUploadTab('paste')">Paste Content</button>
            </div>

            <div id="tab-file" class="tab-content">
                <div id="drop-area" class="file-drop-area">
                    <div class="drop-msg">
                        <span style="font-size:2em;">☁️</span><br>
                        Drag & Drop files here<br>
                        <span style="font-size:0.8em; color:#9ca3af;">or</span>
                    </div>
                    <input type="file" id="overlay-file-input" multiple onchange="updateFileListUI()">
                    <div id="selected-files-list" class="file-list-preview"></div>
                </div>
            </div>

            <div id="tab-paste" class="tab-content hidden">
                <div class="form-group">
                    <label>Filename</label>
                    <input type="text" id="paste-filename" placeholder="example.txt">
                </div>
                <div class="form-group">
                    <label>Content</label>
                    <textarea id="paste-content" placeholder="Paste text here..."></textarea>
                </div>
            </div>

            <div id="upload-progress-container" class="upload-progress-container"></div>
            
            <div id="upload-msg" class="upload-msg"></div>
            
            <div class="modal-actions">
                <button class="btn" onclick="closeUploadOverlay()">Cancel</button>
                <button class="btn btn-primary" onclick="performOverlayUpload('${targetFolder}')">Upload</button>
            </div>
        </div>
    `;

    // Add Drag & Drop Event Listeners
    const dropArea = document.getElementById('drop-area');

    ['dragenter', 'dragover', 'dragleave', 'drop'].forEach(eventName => {
        dropArea.addEventListener(eventName, preventDefaults, false);
    });

    ['dragenter', 'dragover'].forEach(eventName => {
        dropArea.addEventListener(eventName, () => dropArea.classList.add('highlight'), false);
    });

    ['dragleave', 'drop'].forEach(eventName => {
        dropArea.addEventListener(eventName, () => dropArea.classList.remove('highlight'), false);
    });

    dropArea.addEventListener('drop', handleDrop, false);

    overlay.classList.remove('hidden');
    overlay.style.display = 'flex';
}

// Global scope needed for onclick handlers in HTML string
window.switchUploadTab = function (mode) {
    const tabFile = document.getElementById('tab-file');
    const tabPaste = document.getElementById('tab-paste');
    const btns = document.querySelectorAll('.tab-btn');

    if (mode === 'file') {
        tabFile.classList.remove('hidden');
        tabPaste.classList.add('hidden');
        btns[0].classList.add('active');
        btns[1].classList.remove('active');
    } else {
        tabFile.classList.add('hidden');
        tabPaste.classList.remove('hidden');
        btns[0].classList.remove('active');
        btns[1].classList.add('active');
    }
};

window.closeUploadOverlay = function () {
    const overlay = document.getElementById('upload-overlay');
    if (overlay) {
        overlay.classList.add('hidden');
        overlay.style.display = 'none';
    }
};

// New Helper: Upload with Progress Callback
function uploadFileWithProgress(file, folder, onProgress) {
    return new Promise((resolve, reject) => {
        const fullPath = folder.endsWith('/') ? folder + file.name : folder + '/' + file.name;
        const renamedFile = new File([file], fullPath, { type: file.type });

        const formData = new FormData();
        formData.append("file", renamedFile);

        const xhr = new XMLHttpRequest();
        xhr.open("POST", "/system/upload");

        xhr.upload.addEventListener("progress", (e) => {
            if (e.lengthComputable) {
                const percent = (e.loaded / e.total) * 100;
                if (onProgress) onProgress(percent);
            }
        });

        xhr.onload = () => {
            if (xhr.status === 200) resolve();
            else reject("Server Error: " + xhr.status);
        };

        xhr.onerror = () => reject("Network Error");
        xhr.send(formData);
    });
}

// UPDATED: Handle Multiple Files + Progress display
window.performOverlayUpload = async function (folder) {
    const isFileMode = !document.getElementById('tab-file').classList.contains('hidden');
    const progressContainer = document.getElementById('upload-progress-container');
    const msgDiv = document.getElementById('upload-msg');

    progressContainer.innerHTML = ''; // Clear previous progress
    msgDiv.innerText = '';
    msgDiv.className = 'upload-msg';

    if (isFileMode) {
        const fileInput = document.getElementById('overlay-file-input');
        const files = fileInput.files;

        if (files.length === 0) {
            msgDiv.innerText = "Please select at least one file.";
            msgDiv.className = "upload-msg error";
            return;
        }

        let failCount = 0;

        // Process files sequentially (or parallel if preferred, sequential is safer for ESP32)
        for (let i = 0; i < files.length; i++) {
            const file = files[i];

            // Create Progress UI Item
            const progressItem = document.createElement('div');
            progressItem.className = 'upload-progress-item';
            progressItem.innerHTML = `
                <div class="progress-label">
                    <span class="fname">${escapeHtml(file.name)}</span>
                    <span class="pct">0%</span>
                </div>
                <div class="progress-bar-sm">
                    <div class="progress-fill-sm" style="width:0%"></div>
                </div>
            `;
            progressContainer.appendChild(progressItem);

            const fill = progressItem.querySelector('.progress-fill-sm');
            const pct = progressItem.querySelector('.pct');

            try {
                // Use new helper that accepts a callback
                await uploadFileWithProgress(file, folder, (percent) => {
                    fill.style.width = percent + "%";
                    pct.innerText = Math.round(percent) + "%";
                });

                pct.innerText = "✓";
                pct.style.color = "var(--charging-color)"; // Green
            } catch (err) {
                failCount++;
                pct.innerText = "✗";
                pct.style.color = "var(--danger)"; // Red
                console.error(err);
            }
        }

        if (failCount === 0) {
            msgDiv.innerText = "All uploads complete!";
            setTimeout(() => {
                closeUploadOverlay();
                fetchFiles();
            }, 1000);
        } else {
            msgDiv.innerText = `Completed with ${failCount} errors.`;
            msgDiv.className = "upload-msg error";
        }

    } else {
        // Paste Mode (Single file logic preserved)
        const name = document.getElementById('paste-filename').value.trim();
        const content = document.getElementById('paste-content').value;

        if (!name) {
            msgDiv.innerText = "Filename is required.";
            msgDiv.className = "upload-msg error";
            return;
        }

        const fileToUpload = new File([content], name, { type: "text/plain" });

        // UI for single paste upload
        const progressItem = document.createElement('div');
        progressItem.className = 'upload-progress-item';
        progressItem.innerHTML = `
            <div class="progress-label"><span>${escapeHtml(name)}</span><span class="pct">0%</span></div>
            <div class="progress-bar-sm"><div class="progress-fill-sm" style="width:0%"></div></div>
        `;
        progressContainer.appendChild(progressItem);

        const fill = progressItem.querySelector('.progress-fill-sm');

        try {
            await uploadFileWithProgress(fileToUpload, folder, (p) => fill.style.width = p + "%");
            closeUploadOverlay();
            fetchFiles();
        } catch (err) {
            msgDiv.innerText = "Upload Failed: " + err;
            msgDiv.className = "upload-msg error";
        }
    }
};


// --- Tree Logic ---

function buildTreeStructure(files) {
    const root = { name: "sdcard", path: "/sdcard", isDir: true, children: {} };

    files.forEach(file => {
        let cleanPath = file.name;
        if (cleanPath.startsWith("/sdcard/")) cleanPath = cleanPath.substring(8);
        else if (cleanPath.startsWith("/")) cleanPath = cleanPath.substring(1);

        const parts = cleanPath.split('/').filter(p => p.length > 0);

        let currentLevel = root;
        let builtPath = "/sdcard";

        parts.forEach((part, index) => {
            builtPath += "/" + part;

            if (!currentLevel.children[part]) {
                currentLevel.children[part] = {
                    name: part,
                    path: builtPath,
                    isDir: true,
                    children: {},
                    size: 0
                };
            }

            if (index === parts.length - 1) {
                currentLevel.children[part].isDir = file.isDir;
                currentLevel.children[part].size = file.size;
            }

            currentLevel = currentLevel.children[part];
        });
    });
    return root;
}

function renderTree(files) {
    const container = document.getElementById('file-tree');
    container.innerHTML = '';
    const rootNode = buildTreeStructure(files);
    renderTreeRecursive(rootNode, container, 0);
}

function renderTreeRecursive(node, container, depth) {
    const nodeEl = document.createElement('div');
    nodeEl.className = 'tree-node';

    const header = document.createElement('div');
    header.className = 'tree-header';
    header.style.paddingLeft = (depth * 20) + "px";

    // 1. Toggle Icon
    const toggleIcon = document.createElement('span');
    toggleIcon.className = 'toggle-icon';
    toggleIcon.innerText = node.isDir ? '▶' : ' ';
    header.appendChild(toggleIcon);

    // 2. Icon + Name
    const label = document.createElement('span');
    label.className = 'node-label';
    let displayName = node.name;
    if (node.isDir) {
        const count = Object.keys(node.children).length;
        displayName += ` (${count})`;
    }
    label.textContent = (node.isDir ? '📁 ' : '📄 ') + displayName;
    header.appendChild(label);

    // 3. Controls
    const controls = document.createElement('div');
    controls.className = 'node-controls';


    if (node.isDir) {
        // controls.appendChild(createCopyBtn());

        const checkbox = document.createElement('input');
        checkbox.type = 'checkbox';
        checkbox.className = 'folder-check';
        if (selectedFolders.has(node.path)) checkbox.checked = true;

        checkbox.onclick = (e) => {
            e.stopPropagation();
            if (checkbox.checked) selectedFolders.add(node.path);
            else selectedFolders.delete(node.path);
            updateToolbar();
        };
        controls.appendChild(checkbox);

    } else {
        const sizeSpan = document.createElement('span');
        sizeSpan.className = 'file-size';
        sizeSpan.innerText = humanSize(node.size);
        controls.appendChild(sizeSpan);

        // controls.appendChild(createCopyBtn());

        const openBtn = document.createElement('button');
        openBtn.className = 'btn-icon';
        openBtn.innerText = '↗';
        openBtn.title = 'Open / Actions';
        openBtn.onclick = (e) => {
            e.stopPropagation();
            openFileActionOverlay(node.path);
        };
        controls.appendChild(openBtn);

        const delBtn = document.createElement('button');
        delBtn.className = 'btn-icon danger';
        delBtn.innerText = '🗑️';
        delBtn.title = 'Delete';
        delBtn.onclick = (e) => { e.stopPropagation(); deleteFile(node.path); };
        controls.appendChild(delBtn);
    }

    header.appendChild(controls);
    nodeEl.appendChild(header);

    // 4. Children
    const childrenContainer = document.createElement('div');
    childrenContainer.className = 'tree-children hidden';

    if (node.isDir) {
        const childrenKeys = Object.keys(node.children).sort((a, b) => {
            const nodeA = node.children[a];
            const nodeB = node.children[b];
            if (nodeA.isDir && !nodeB.isDir) return -1;
            if (!nodeA.isDir && nodeB.isDir) return 1;
            return a.localeCompare(b);
        });

        childrenKeys.forEach(key => {
            renderTreeRecursive(node.children[key], childrenContainer, depth + 1);
        });

        if (depth < 2) {
            childrenContainer.classList.remove('hidden');
            toggleIcon.innerText = '▼';
            toggleIcon.classList.add('open');
        }

        header.addEventListener('click', (e) => {
            if (['INPUT', 'BUTTON', 'SELECT', 'OPTION'].includes(e.target.tagName)) return;

            const isClosed = childrenContainer.classList.contains('hidden');
            if (isClosed) {
                childrenContainer.classList.remove('hidden');
                toggleIcon.innerText = '▼';
                toggleIcon.classList.add('open');
            } else {
                childrenContainer.classList.add('hidden');
                toggleIcon.innerText = '▶';
                toggleIcon.classList.remove('open');
            }
        });
    }

    nodeEl.appendChild(childrenContainer);
    container.appendChild(nodeEl);
}

// --- Overlay & Actions ---

function openFileActionOverlay(path) {
    currentFileContext = path;
    const filename = path.split('/').pop();

    let overlay = document.getElementById('file-action-overlay');
    if (!overlay) {
        overlay = document.createElement('div');
        overlay.id = 'file-action-overlay';
        overlay.className = 'overlay';
        document.body.appendChild(overlay);
    }

    overlay.className = 'overlay';
    overlay.style.display = 'flex';

    overlay.innerHTML = `
        <div class="modal" style="width: 380px;">
            <h3>Actions for ${escapeHtml(filename)}</h3>
            <div class="loading">Loading tools...</div>
            <div class="modal-actions" style="margin-top:20px;">
                <button class="btn" onclick="closeFileOverlay()">Close</button>
            </div>
        </div>
    `;

    fetch('/dynamic/tools.json')
        .then(res => res.json())
        .then(data => {
            availableTools = data.tools || [];
            renderFileOverlayContent(filename);
        })
        .catch(err => {
            console.error("Failed to load tools", err);
            availableTools = [];
            renderFileOverlayContent(filename);
        });
}

function renderFileOverlayContent(filename) {
    const overlay = document.getElementById('file-action-overlay');
    if (!overlay) return;

    const epyEditors = availableTools.filter(t => t.epyTool === true && t.isEditor === true);
    const currentExt = filename.split('.').pop().toLowerCase();

    let toolsHtml = '';
    if (epyEditors.length > 0) {
        let bestMatch = epyEditors.find(t => t.extensions && t.extensions.split(',').map(e => e.trim().toLowerCase()).includes(currentExt));
        if (!bestMatch) bestMatch = epyEditors[0];

        const selectedEndpoint = bestMatch.endpoint;

        let options = epyEditors.map(t => {
            const isSelected = t.endpoint === selectedEndpoint ? 'selected' : '';
            return `<option value="${t.endpoint}" ${isSelected}>${t.name}</option>`;
        }).join('');
        toolsHtml = `
            <div class="form-group" style="margin-top: 15px; background: #f9fafb; padding: 15px; border-radius: 8px;">
                <label style="display:block; margin-bottom:5px; font-weight:600; font-size:0.9rem;">Open with an EPY Editor</label>
                <div style="display:flex; gap:10px;">
                    <select id="file-tool-select" style="flex-grow:1; padding: 6px; border-radius:4px; border:1px solid #ccc;">
                        ${options}
                    </select>
                    <button class="btn btn-primary" onclick="launchFileTool()">Open</button>
                </div>
            </div>
        `;
    } else {
        toolsHtml = `<p style="color:#666; font-style:italic; margin: 15px 0;">No EPY Editors available.</p>`;
    }

    const modalContent = `
        <div class="modal" style="width: 380px;">
            <h3 style="word-break: break-all;">${escapeHtml(filename)}</h3>
            
            ${toolsHtml}
            
            <div style="margin-top: 20px; border-top: 1px solid #eee; padding-top: 15px; display: flex; justify-content: space-between; gap: 10px;">
                <button class="btn btn-download" onclick="triggerContextDownload()">
                    <span style="font-size:1.1em; vertical-align:middle;">⬇</span> Download File
                </button>
                <button class="btn" onclick="closeFileOverlay()">Close</button>
            </div>
        </div>
    `;

    overlay.innerHTML = modalContent;
}

function launchFileTool() {
    const select = document.getElementById('file-tool-select');
    if (!select) return;

    const endpoint = select.value;
    if (!endpoint) return;

    const separator = endpoint.includes('?') ? '&' : '?';
    const url = `${endpoint}${separator}filePath=${encodeURIComponent(currentFileContext)}`;

    window.open(url, '_blank');
    closeFileOverlay();
}

function triggerContextDownload() {
    downloadFile(currentFileContext);
}

function closeFileOverlay() {
    const overlay = document.getElementById('file-action-overlay');
    if (overlay) {
        overlay.style.display = 'none';
        overlay.classList.add('hidden');
    }
}

function showToast(msg) {
    let toast = document.getElementById('toast-msg');
    if (!toast) {
        toast = document.createElement('div');
        toast.id = 'toast-msg';
        toast.style.position = 'fixed';
        toast.style.bottom = '20px';
        toast.style.right = '20px';
        toast.style.background = '#333';
        toast.style.color = '#fff';
        toast.style.padding = '10px 20px';
        toast.style.borderRadius = '5px';
        toast.style.zIndex = '2000';
        toast.style.opacity = '0';
        toast.style.transition = 'opacity 0.3s';
        document.body.appendChild(toast);
    }
    toast.innerText = msg;
    toast.style.opacity = '1';
    setTimeout(() => { toast.style.opacity = '0'; }, 2000);
}

// Changed to support File Object upload (Used by both legacy and Overlay)
function uploadSingleFile(fileObj, folder) {
    return new Promise((resolve, reject) => {
        const fullPath = folder.endsWith('/') ? folder + fileObj.name : folder + '/' + fileObj.name;
        const renamedFile = new File([fileObj], fullPath, { type: fileObj.type });

        const formData = new FormData();
        formData.append("file", renamedFile);

        const xhr = new XMLHttpRequest();
        xhr.open("POST", "/system/upload");

        xhr.onload = () => {
            if (xhr.status === 200) resolve();
            else reject("Server Error: " + xhr.status);
        };

        xhr.onerror = () => reject("Network Error");
        xhr.send(formData);
    });
}

function downloadFile(path) {
    window.open('/system/file?name=' + encodeURIComponent(path));
}

function deleteFile(path) {
    const confirmFn = window.showConfirm || ((msg, cb) => { if (confirm(msg)) cb(); });

    confirmFn("Delete file " + path + "?", () => {
        fetch('/system/file?name=' + encodeURIComponent(path), { method: 'DELETE' })
            .then(res => {
                if (res.ok) fetchFiles();
                else alert("Failed to delete.");
            })
            .catch(err => alert("Connection Failed"));
    });
}

function humanSize(bytes) {
    if (bytes < 1024) return bytes + " B";
    else if (bytes < 1048576) return (bytes / 1024).toFixed(2) + " KB";
    else return (bytes / 1048576).toFixed(2) + " MB";
}

function escapeHtml(unsafe) {
    if (typeof unsafe !== 'string') return unsafe;
    return unsafe
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#039;");
}