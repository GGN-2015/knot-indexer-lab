export default {
  data() {
    return {
      desc_for_index_by_name_1:
        "Given the <strong>name of a knot</strong>, retrieve various details about the knot, including the <strong>PD notation</strong>, <strong>HOMFLY-PT polynomial</strong>, and <strong>Khovanov homology</strong>.",
      desc_for_index_by_name_2:
        "We use the notation <code>Kxay</code> to describe an alternating knot with x crossings, and <code>Kxny</code> to describe a non-alternating knot with x crossings. For example, <code>K3a1</code> and <code>K8n1</code>. To denote the mirror image of knot <code>Kxay</code>, we use <code>mKxay</code>, and similarly for <code>Kxny</code>, we use <code>mKxny</code>.",
      desc_for_index_by_name_3:
        "To enter a composite knot, please provide its prime factorization form by listing the names of several prime knots separated by commas, and use them as the name for this composite knot. For example, <code>K3a1,mK3a1</code> stands for the connected sum of <code>K3a1</code> and <code>mK3a1</code>.",

      desc_for_index_by_pd_1:
        "Given a <strong>PD notation</strong>, compute the <strong>HOMFLY-PT polynomial</strong> and <strong>Khovanov homology</strong> of the knot, and search the database for all potential <strong>knot names</strong>.",
      desc_for_index_by_pd_2:
        "PD notation should be represented as a nested list. A valid representation would be, for example, <code>[[1, 5, 2, 4], [3, 1, 4, 6], [5, 3, 6, 2]]</code> (which represents a trefoil).",

      desc_for_index_by_coord_1:
        "Provided with a series of coordinates in three-dimensional space, where each line consists of <strong>three decimal numbers</strong> separated by spaces, and with multiple lines in total, the server will connect all the coordinates in sequence, joining the first and last coordinates to form a knot, and then calculate its PD notation.",
      desc_for_index_by_coord_2:
        "Afterwards, the server will use the obtained PD notation to calculate the <strong>HOMFLY-PT polynomial</strong> and <strong>Khovanov homology</strong>, and make an inference about the <strong>name of the knot</strong>.",

      knot_name_search_status: "",
      modal_title: "",
      modal_content: "",
      admin_email: "premierbob@qq.com",

      knot_name_list: "...",
      knot_pd_code: "...",
      knot_pd_diagram_svg: "",
      knot_pd_diagram_error: "",
      diagramViewerOpen: false,
      diagramViewerZoom: 1,
      diagramViewerTranslateX: 0,
      diagramViewerTranslateY: 0,
      diagramViewerDragging: false,
      diagramViewerDragStartX: 0,
      diagramViewerDragStartY: 0,
      diagramViewerStartX: 0,
      diagramViewerStartY: 0,
      homflypt_polynomial: "...",
      khovanov_homology: "...",
      taskSocket: null,
      taskReconnectTimer: null,
      restoredTaskSignature: ""
    };
  },
  mounted() {
    this.connectTaskSocket();
    document.addEventListener("keydown", this.handleDiagramViewerKeydown);
  },
  unmounted() {
    document.removeEventListener("keydown", this.handleDiagramViewerKeydown);
    this.unlockPageScroll();
    this.closeTaskSocket();
  },
  computed: {
    diagramViewerTransform() {
      return `translate(${this.diagramViewerTranslateX}px, ${this.diagramViewerTranslateY}px) scale(${this.diagramViewerZoom})`;
    }
  },
  methods: {
    replaceAllText(text, from, to) {
      return text.split(from).join(to);
    },
    encodePayload(text) {
      return encodeURIComponent(btoa(text));
    },
    async getJson(url, options = {}) {
      const response = await fetch(url, options);
      if (!response.ok) {
        return { status: "error", message: "network error." };
      }
      return await response.json();
    },
    taskSocketUrl() {
      const scheme = window.location.protocol === "https:" ? "wss:" : "ws:";
      return `${scheme}//${window.location.host}/ws/tasks`;
    },
    connectTaskSocket() {
      if (!("WebSocket" in window)) return;
      if (this.taskSocket && (this.taskSocket.readyState === WebSocket.OPEN || this.taskSocket.readyState === WebSocket.CONNECTING)) return;
      this.taskSocket = new WebSocket(this.taskSocketUrl());
      this.taskSocket.onmessage = (event) => {
        try {
          const snapshot = JSON.parse(event.data);
          this.restoreLatestSessionTask(snapshot.last_session_task);
        } catch (error) {
          console.log(error);
        }
      };
      this.taskSocket.onclose = () => {
        this.taskSocket = null;
        this.scheduleTaskSocketReconnect();
      };
      this.taskSocket.onerror = () => {
        if (this.taskSocket) this.taskSocket.close();
      };
    },
    scheduleTaskSocketReconnect() {
      if (this.taskReconnectTimer) return;
      this.taskReconnectTimer = setTimeout(() => {
        this.taskReconnectTimer = null;
        this.connectTaskSocket();
      }, 1500);
    },
    closeTaskSocket() {
      if (this.taskReconnectTimer) {
        clearTimeout(this.taskReconnectTimer);
        this.taskReconnectTimer = null;
      }
      if (this.taskSocket) {
        const socket = this.taskSocket;
        this.taskSocket = null;
        socket.onclose = null;
        socket.close();
      }
    },
    taskResultText(status, value, error) {
      if (status === "success") return value || "";
      if (!status || status === "pending") return "...";
      return status.toUpperCase() + (error ? ": " + error : "");
    },
    taskSignature(task) {
      if (!task) return "";
      return [
        task.id,
        task.status,
        task.canonical_pd,
        task.pd_diagram_svg,
        task.pd_diagram_error,
        task.knot_types,
        task.homfly_status,
        task.homfly_result,
        task.homfly_error,
        task.khovanov_status,
        task.khovanov_result,
        task.khovanov_error,
        task.error
      ].join("|");
    },
    restoreLatestSessionTask(task) {
      if (!task) return;
      const signature = this.taskSignature(task);
      if (signature === this.restoredTaskSignature) return;
      this.restoredTaskSignature = signature;

      this.knot_pd_code = task.canonical_pd || "...";
      this.knot_pd_diagram_svg = task.pd_diagram_svg || "";
      this.knot_pd_diagram_error = task.pd_diagram_error || "";
      if (task.knot_types) {
        this.knot_name_list = task.knot_types;
      } else if (task.status === "running") {
        this.knot_name_list = "...";
      } else {
        this.knot_name_list = task.error || "NOT_FOUND";
      }
      this.homflypt_polynomial = this.taskResultText(task.homfly_status, task.homfly_result, task.homfly_error);
      this.khovanov_homology = this.expandComma(this.taskResultText(task.khovanov_status, task.khovanov_result, task.khovanov_error));

      if (task.status === "running") {
        this.startSearchingSpinner();
      } else {
        this.knot_name_search_status = "";
      }
    },
    async getIndexByPdCode(pdCode) {
      try {
        return await this.getJson('/api/index_pd_code/' + this.encodePayload(pdCode));
      } catch (error) {
        console.log(error);
        return { status: "error", message: "network error." };
      }
    },
    async getIndexByKnotName(knotName) {
      try {
        return await this.getJson('/api/index_knot_name/' + this.encodePayload(knotName));
      } catch (error) {
        console.log(error);
        return { status: "error", message: "network error." };
      }
    },
    async getIndexByCoord3d(coord3d) {
      try {
        return await this.getJson("/api/index_coord_3d", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ coord_3d: coord3d })
        });
      } catch (error) {
        console.log(error);
        return { status: "error", message: "network error." };
      }
    },
    formatInvariant(status, value, error) {
      if (status === "success") return value;
      if (!status || status === "pending") return "...";
      return status.toUpperCase() + (error ? ": " + error : "");
    },
    applyIndexResult(result, fallbackName = "") {
      this.knot_name_list = result.knot_name || fallbackName || "NOT_FOUND";
      this.knot_pd_code = result.pd_code || "...";
      this.knot_pd_diagram_svg = result.pd_diagram_svg || "";
      this.knot_pd_diagram_error = result.pd_diagram_error || "";
      this.homflypt_polynomial = this.formatInvariant(result.homfly_status, result.homflypt_polynomial, result.homfly_error);
      this.khovanov_homology = this.expandComma(this.formatInvariant(result.khovanov_status, result.khovanov_homology, result.khovanov_error));
    },
    unifyKnotName() {
      let knotName = document.getElementById("knot_name").value.toLowerCase();
      knotName = this.replaceAllText(knotName, "k", "K");
      knotName = this.replaceAllText(knotName, " ", "");
      document.getElementById("knot_name").value = knotName;
      return knotName;
    },
    async getKnotNameInfo(knotName) {
      if (knotName === "") {
        return { status: "error", message: "knot name should not be empty." };
      }
      try {
        return await this.getJson('/api/knot_name2pd_code/' + this.encodePayload(knotName));
      } catch (error) {
        console.log(error);
        return { status: "error", message: "network error." };
      }
    },
    async delay(ms) {
      return new Promise(resolve => setTimeout(resolve, ms));
    },
    async showMessageBox(modalTitle, modalContent) {
      const staticBackdrop = document.getElementById("staticBackdrop");
      const rect = staticBackdrop.getBoundingClientRect();
      const isVisible = rect.width > 0 || rect.height > 0;

      if (isVisible) {
        document.getElementById("close_modal_button").click();
        await this.delay(300);
      }

      this.modal_title = modalTitle;
      this.modal_content = modalContent;
      document.getElementById("button_show_modal").click();
    },
    clearKnotInfoData() {
      this.knot_name_list = "...";
      this.knot_pd_code = "...";
      this.knot_pd_diagram_svg = "";
      this.knot_pd_diagram_error = "";
      this.closeDiagramViewer();
      this.homflypt_polynomial = "...";
      this.khovanov_homology = "...";
    },
    async getHomflyptInfo(pdCode) {
      try {
        return await this.getJson('/api/pd_code2homflypt/' + this.encodePayload(pdCode));
      } catch (error) {
        console.log(error);
        return { status: "error", message: "network error." };
      }
    },
    async getKhovanovInfo(pdCode) {
      try {
        return await this.getJson('/api/pd_code2khovanov/' + this.encodePayload(pdCode));
      } catch (error) {
        console.log(error);
        return { status: "error", message: "network error." };
      }
    },
    async getHomflyptByPdCode(pdCode) {
      const homflyptInfo = await this.getHomflyptInfo(pdCode);
      if (homflyptInfo.status === "success") {
        this.homflypt_polynomial = homflyptInfo.message;
      } else {
        this.showMessageBox("error", "get_homflypt_by_pdcode: " + homflyptInfo.message);
      }
    },
    expandComma(text) {
      return text.split(",").join(", ");
    },
    async getKhovanovByPdCode(pdCode) {
      const khovanovInfo = await this.getKhovanovInfo(pdCode);
      if (khovanovInfo.status === "success") {
        this.khovanov_homology = this.expandComma(khovanovInfo.message);
      } else {
        this.showMessageBox("error", "get_khovanov_by_pdcode: " + khovanovInfo.message);
      }
    },
    getInputPdNotation() {
      let pdNotation = document.getElementById("pd_notation_box").value;
      pdNotation = pdNotation.toLowerCase();
      pdNotation = this.replaceAllText(pdNotation, "pd", "");
      pdNotation = this.replaceAllText(pdNotation, "x", "");
      pdNotation = this.replaceAllText(pdNotation, " ", "");
      document.getElementById("pd_notation_box").value = pdNotation;
      return pdNotation;
    },
    async getKnotNameMatchInfo(pdCode) {
      if (pdCode === "") {
        return { status: "error", message: "pd notation should not be empty." };
      }
      try {
        return await this.getJson('/api/pd_code2knot_name/' + this.encodePayload(pdCode));
      } catch (error) {
        console.log(error);
        return { status: "error", message: "network error." };
      }
    },
    async getKnotNameByPdCode(pdNotation) {
      const knotNameMatchInfo = await this.getKnotNameMatchInfo(pdNotation);
      if (knotNameMatchInfo.status === "error") {
        this.showMessageBox("error", "get_knotname_by_pdcode: " + knotNameMatchInfo.message);
      } else {
        this.knot_name_list = knotNameMatchInfo.message || "NOT_FOUND";
      }
    },
    startSearchingSpinner() {
      this.knot_name_search_status = `
        <div class="alert alert-success" role="alert">
          <div class="row">
            <div class="col mt-2">
              <div class="spinner-border text-success" role="status"><span class="visually-hidden">Loading...</span></div>
            </div>
            <div class="col-10 mt-2">
              <p>Calculating ... refreshing is safe; this browser will reconnect automatically.</p>
            </div>
          </div>
        </div>`;
    },
    async getPdCodeByCoord3d(coord3d) {
      if (coord3d === "") {
        return { status: "error", message: "coord_3d should not be empty." };
      }
      try {
        return await this.getJson("/api/coord_3d2pd_code", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ coord_3d: coord3d })
        });
      } catch (error) {
        console.log(error);
        return { status: "error", message: "network error." };
      }
    },
    async coord3dSubmit() {
      this.clearKnotInfoData();

      let coord3d = document.getElementById("coord_3d_box").value;
      coord3d = this.replaceAllText(coord3d, "];", "\n");
      coord3d = this.replaceAllText(coord3d, "[", "");
      coord3d = this.replaceAllText(coord3d, ";", " ");
      coord3d = this.replaceAllText(coord3d, "]", "");
      document.getElementById("coord_3d_box").value = coord3d;
      this.startSearchingSpinner();
      const result = await this.getIndexByCoord3d(coord3d);
      this.applyIndexResult(result);
      if (result.status === "error") {
        this.showMessageBox("error", "coord3d_submit: " + result.message);
      }
      await this.stopSearchingSpinner();
    },
    async pdNotationSubmit() {
      if (this.knot_name_search_status !== "") {
        this.showMessageBox("error", "pd_notation_submit: request already submitted.");
        return;
      }
      this.clearKnotInfoData();

      const pdNotation = this.getInputPdNotation();
      if (pdNotation === "") {
        this.showMessageBox("error", "get_homflypt_by_pdcode: pd notation should not be empty.");
        return;
      }

      this.startSearchingSpinner();
      const result = await this.getIndexByPdCode(pdNotation);
      this.applyIndexResult(result);
      if (result.status === "error") {
        this.showMessageBox("error", "pd_notation_submit: " + result.message);
      }
      await this.stopSearchingSpinner();
    },
    async stopSearchingSpinner() {
      await this.delay(1000);
      this.knot_name_search_status = "";
    },
    async knotNameSubmit() {
      if (this.knot_name_search_status !== "") {
        this.showMessageBox("error", "knot_name_submit: request already submitted.");
        return;
      }

      this.clearKnotInfoData();
      const knotName = this.unifyKnotName();
      if (knotName !== "") {
        this.startSearchingSpinner();
      }

      const result = await this.getIndexByKnotName(knotName);
      this.applyIndexResult(result, knotName);
      if (result.status === "error") {
        this.showMessageBox("error", "knot_name_submit: " + result.message);
      }
      await this.stopSearchingSpinner();
    },
    async copyKnotNameList() {
      await navigator.clipboard.writeText(this.knot_name_list);
      alert("copied");
    },
    async copyKnotPdCode() {
      await navigator.clipboard.writeText(this.knot_pd_code);
      alert("copied");
    },
    async copyHomflyptPolynomial() {
      await navigator.clipboard.writeText(this.homflypt_polynomial);
      alert("copied");
    },
    async copyKhovanovHomology() {
      await navigator.clipboard.writeText(this.khovanov_homology);
      alert("copied");
    },
    lockPageScroll() {
      document.body.dataset.previousOverflow = document.body.style.overflow || "";
      document.body.style.overflow = "hidden";
    },
    unlockPageScroll() {
      if (document.body.dataset.previousOverflow !== undefined) {
        document.body.style.overflow = document.body.dataset.previousOverflow;
        delete document.body.dataset.previousOverflow;
      }
    },
    openDiagramViewer() {
      if (!this.knot_pd_diagram_svg) return;
      if (this.diagramViewerOpen) return;
      this.diagramViewerOpen = true;
      this.diagramViewerZoom = 1;
      this.diagramViewerTranslateX = 0;
      this.diagramViewerTranslateY = 0;
      this.diagramViewerDragging = false;
      this.lockPageScroll();
    },
    closeDiagramViewer() {
      if (!this.diagramViewerOpen) return;
      this.diagramViewerOpen = false;
      this.diagramViewerDragging = false;
      this.unlockPageScroll();
    },
    handleDiagramViewerKeydown(event) {
      if (!this.diagramViewerOpen) return;
      if (event.key === "Escape") {
        event.preventDefault();
        this.closeDiagramViewer();
      }
    },
    resetDiagramViewer() {
      this.diagramViewerZoom = 1;
      this.diagramViewerTranslateX = 0;
      this.diagramViewerTranslateY = 0;
    },
    zoomDiagramViewer(delta) {
      const nextZoom = Math.min(8, Math.max(0.25, this.diagramViewerZoom * delta));
      this.diagramViewerZoom = Number(nextZoom.toFixed(3));
    },
    wheelDiagramViewer(event) {
      const delta = event.deltaY < 0 ? 1.12 : 0.89;
      this.zoomDiagramViewer(delta);
    },
    startDiagramPan(event) {
      if (!this.diagramViewerOpen) return;
      this.diagramViewerDragging = true;
      this.diagramViewerDragStartX = event.clientX;
      this.diagramViewerDragStartY = event.clientY;
      this.diagramViewerStartX = this.diagramViewerTranslateX;
      this.diagramViewerStartY = this.diagramViewerTranslateY;
      if (event.currentTarget.setPointerCapture) {
        event.currentTarget.setPointerCapture(event.pointerId);
      }
    },
    moveDiagramPan(event) {
      if (!this.diagramViewerDragging) return;
      this.diagramViewerTranslateX = this.diagramViewerStartX + event.clientX - this.diagramViewerDragStartX;
      this.diagramViewerTranslateY = this.diagramViewerStartY + event.clientY - this.diagramViewerDragStartY;
    },
    endDiagramPan(event) {
      this.diagramViewerDragging = false;
      if (event.currentTarget.releasePointerCapture) {
        try {
          event.currentTarget.releasePointerCapture(event.pointerId);
        } catch (error) {
          // The pointer may already be released by the browser.
        }
      }
    },
  },
  template: `
    <div class="alert alert-warning d-flex align-items-center" role="alert">
      <svg class="bi bi-exclamation-triangle text-success" width="32" height="32" fill="currentColor" viewBox="0 0 16 16" xmlns="http://www.w3.org/2000/svg">
        <path d="M7.938 2.016A.13.13 0 0 1 8.002 2a.13.13 0 0 1 .063.016.15.15 0 0 1 .054.057l6.857 11.667c.036.061.038.13.002.192a.2.2 0 0 1-.169.068H1.195a.2.2 0 0 1-.17-.068.18.18 0 0 1 .003-.192L7.884 2.073a.15.15 0 0 1 .054-.057m1.044-.45a1.13 1.13 0 0 0-1.96 0L.165 13.233c-.457.778.091 1.767.98 1.767h13.713c.889 0 1.438-.99.98-1.767z"/>
        <path d="M7.002 12a1 1 0 1 1 2 0 1 1 0 0 1-2 0M7.1 5.995a.905.905 0 1 1 1.8 0l-.35 3.507a.552.552 0 0 1-1.1 0z"/>
      </svg>
      <div class="ms-2">
        The function of <code>knot-indexer-lab</code> is still under testing, bug report: <code>{{ admin_email }}</code>
      </div>
    </div>

    <div class="d-flex justify-content-end mb-2">
      <a class="btn btn-outline-info" href="/tasks.html">Task Monitor</a>
    </div>

    <div class="accordion mt-2" id="accordionExample">
      <div class="accordion-item">
        <h2 class="accordion-header">
          <button class="accordion-button" type="button" data-bs-toggle="collapse" data-bs-target="#collapseOne" aria-expanded="true" aria-controls="collapseOne">
            Index By Knot Name
          </button>
        </h2>
        <div id="collapseOne" class="accordion-collapse collapse show" data-bs-parent="#accordionExample">
          <div class="accordion-body">
            <ul class="list-group">
              <li class="list-group-item">
                  <p v-html="desc_for_index_by_name_1"></p>
              </li>
              <li class="list-group-item">
                  <p v-html="desc_for_index_by_name_2"></p>
              </li>
              <li class="list-group-item">
                  <p v-html="desc_for_index_by_name_3"></p>
              </li>
              <li class="list-group-item">
                  <div class="row">
                      <div class="col-10 col-xs-3">
                          <div class="input-group">
                              <span class="input-group-text">Knot Name</span>
                              <textarea id="knot_name" class="form-control" aria-label="Knot Name" rows="1"></textarea>
                          </div>
                      </div>
                      <div class="col">
                          <button class="btn btn-outline-success" @click="knotNameSubmit">Submit</button>
                      </div>
                  </div>
              </li>
            </ul>
          </div>
        </div>
      </div>
      <div class="accordion-item">
        <h2 class="accordion-header">
          <button class="accordion-button collapsed" type="button" data-bs-toggle="collapse" data-bs-target="#collapseTwo" aria-expanded="false" aria-controls="collapseTwo">
            Index By PD-Code
          </button>
        </h2>
        <div id="collapseTwo" class="accordion-collapse collapse" data-bs-parent="#accordionExample">
          <div class="accordion-body">
            <ul class="list-group">
              <li class="list-group-item">
                  <p v-html="desc_for_index_by_pd_1"></p>
              </li>
              <li class="list-group-item">
                  <p v-html="desc_for_index_by_pd_2"></p>
              </li>
              <li class="list-group-item">
                  <div class="row">
                      <div class="col-10 col-xs-3">
                          <div class="input-group">
                              <span class="input-group-text">PD Notation</span>
                              <textarea id="pd_notation_box" class="form-control" aria-label="PD Notation" rows="2"></textarea>
                          </div>
                      </div>
                      <div class="col">
                          <button class="btn btn-outline-success" @click="pdNotationSubmit">Submit</button>
                      </div>
                  </div>
              </li>
            </ul>
          </div>
        </div>
      </div>
      <div class="accordion-item">
        <h2 class="accordion-header">
          <button class="accordion-button collapsed" type="button" data-bs-toggle="collapse" data-bs-target="#collapseThree" aria-expanded="false" aria-controls="collapseThree">
            Index By 3D-Coordinates
          </button>
        </h2>
        <div id="collapseThree" class="accordion-collapse collapse" data-bs-parent="#accordionExample">
          <div class="accordion-body">
            <ul class="list-group">
              <li class="list-group-item">
                  <p v-html="desc_for_index_by_coord_1"></p>
              </li>
              <li class="list-group-item">
                  <p v-html="desc_for_index_by_coord_2"></p>
              </li>
              <li class="list-group-item">
                  <div class="row">
                      <div class="col-10 col-xs-3">
                          <div class="input-group">
                              <span class="input-group-text">Coord 3D</span>
                              <textarea id="coord_3d_box" class="form-control" aria-label="Coord 3D" rows="5"></textarea>
                          </div>
                      </div>
                      <div class="col">
                          <button class="btn btn-outline-success" @click="coord3dSubmit">Submit</button>
                      </div>
                  </div>
              </li>
            </ul>
          </div>
        </div>
      </div>
    </div>

    <div class="row mt-2">
      <div v-html="knot_name_search_status" class="text-center"></div>
    </div>

    <section v-if="knot_pd_diagram_svg || knot_pd_diagram_error" class="pd-diagram-section mt-3">
      <h2 class="h5">PD Diagram</h2>
      <div
        v-if="knot_pd_diagram_svg"
        class="pd-diagram-panel"
        role="button"
        tabindex="0"
        aria-label="Open PD diagram detail"
        @click="openDiagramViewer"
        @keydown.enter.prevent="openDiagramViewer"
        @keydown.space.prevent="openDiagramViewer"
        v-html="knot_pd_diagram_svg">
      </div>
      <div v-if="knot_pd_diagram_error" class="alert alert-warning mt-2 mb-0">{{ knot_pd_diagram_error }}</div>
    </section>

    <div v-if="diagramViewerOpen" class="pd-diagram-viewer" @click.self="closeDiagramViewer">
      <div class="pd-diagram-viewer-toolbar" @click.stop>
        <button type="button" class="btn btn-outline-light btn-sm" aria-label="Zoom out" @click="zoomDiagramViewer(0.8)">-</button>
        <span class="pd-diagram-viewer-zoom">{{ Math.round(diagramViewerZoom * 100) }}%</span>
        <button type="button" class="btn btn-outline-light btn-sm" aria-label="Zoom in" @click="zoomDiagramViewer(1.25)">+</button>
        <button type="button" class="btn btn-outline-light btn-sm" @click="resetDiagramViewer">Reset</button>
        <button type="button" class="btn btn-light btn-sm" @click="closeDiagramViewer">Close</button>
      </div>
      <div
        class="pd-diagram-viewer-stage"
        :class="{ 'is-dragging': diagramViewerDragging }"
        @pointerdown.prevent="startDiagramPan"
        @pointermove.prevent="moveDiagramPan"
        @pointerup="endDiagramPan"
        @pointercancel="endDiagramPan"
        @wheel.prevent="wheelDiagramViewer">
        <div
          class="pd-diagram-viewer-content"
          :style="{ transform: diagramViewerTransform }"
          v-html="knot_pd_diagram_svg">
        </div>
      </div>
    </div>

    <table class="table mt-2">
      <tbody>
        <tr>
          <th scope="row">Possible Knot Name</th>
          <td><code id="code_knot_name_list">{{ knot_name_list }}</code></td>
        </tr>
        <tr>
          <th scope="row">PD Notation</th>
          <td><code id="code_knot_pd_code">{{ knot_pd_code }}</code></td>
        </tr>
        <tr>
          <th scope="row">HOMFLY-PT Polynomial (mirror)</th>
          <td><code id="code_homflypt_polynomial">{{ homflypt_polynomial }}</code></td>
        </tr>
        <tr>
          <th scope="row">Khovanov Homology</th>
          <td><code id="code_khovanov_homology">{{ khovanov_homology }}</code></td>
        </tr>
      </tbody>
    </table>

    <button type="button" id="button_show_modal" class="btn btn-primary" data-bs-toggle="modal" data-bs-target="#staticBackdrop" hidden>
      Launch static backdrop modal
    </button>

    <div class="modal fade" id="staticBackdrop" data-bs-backdrop="static" data-bs-keyboard="false" tabindex="-1" aria-labelledby="staticBackdropLabel" aria-hidden="true">
      <div class="modal-dialog">
        <div class="modal-content">
          <div class="modal-header">
            <h1 class="modal-title fs-5" id="staticBackdropLabel">{{ modal_title }}</h1>
            <button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Close"></button>
          </div>
          <div class="modal-body">
            {{ modal_content }}
          </div>
          <div class="modal-footer">
            <button type="button" id="close_modal_button" class="btn btn-secondary" data-bs-dismiss="modal">Close</button>
          </div>
        </div>
      </div>
    </div>
    `
};
