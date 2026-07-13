export default {
  emits: ["navigate"],
  data() {
    return {
      activeTasks: [],
      completedTasks: [],
      historyCursor: 0,
      historyHasMore: false,
      historyLoading: false,
      error: "",
      refreshTimer: null,
      taskSocket: null,
      reconnectTimer: null,
      socketStatus: "connecting"
    };
  },
  computed: {
    runningTasks() {
      return this.activeTasks.filter(task => task.status === "queued" || task.status === "running");
    },
    finishedTasks() {
      return this.completedTasks;
    }
  },
  async mounted() {
    await this.fetchTasks();
    this.connectTaskSocket();
    this.refreshTimer = setInterval(this.fetchActiveTasks, 10000);
  },
  unmounted() {
    if (this.refreshTimer) clearInterval(this.refreshTimer);
    this.closeTaskSocket();
  },
  methods: {
    async fetchTasks() {
      await Promise.all([this.fetchActiveTasks(), this.fetchHistory(true)]);
    },
    showHome() {
      this.$emit("navigate", "home");
    },
    async fetchActiveTasks() {
      try {
        const response = await fetch("/api/tasks");
        const data = await response.json();
        if (data.status === "success") {
          this.activeTasks = data.tasks || [];
          this.error = "";
        } else {
          this.error = data.message || "could not load tasks.";
        }
      } catch (error) {
        console.log(error);
        this.error = "network error.";
      }
    },
    async fetchHistory(reset = false) {
      if (this.historyLoading) return;
      this.historyLoading = true;
      try {
        const cursor = reset ? 0 : this.historyCursor;
        const response = await fetch(`/api/tasks/history/${cursor}`);
        const data = await response.json();
        if (data.status === "success") {
          const records = data.tasks || [];
          this.completedTasks = reset ? records : this.completedTasks.concat(records);
          this.historyCursor = data.next_cursor || 0;
          this.historyHasMore = Boolean(data.has_more);
          this.error = "";
        } else {
          this.error = data.message || "could not load task history.";
        }
      } catch (error) {
        console.log(error);
        this.error = "network error.";
      } finally {
        this.historyLoading = false;
      }
    },
    taskSocketUrl() {
      const scheme = window.location.protocol === "https:" ? "wss:" : "ws:";
      return `${scheme}//${window.location.host}/ws/tasks`;
    },
    connectTaskSocket() {
      if (!("WebSocket" in window)) {
        this.socketStatus = "polling";
        return;
      }
      if (this.taskSocket && (this.taskSocket.readyState === WebSocket.OPEN || this.taskSocket.readyState === WebSocket.CONNECTING)) return;
      this.socketStatus = "connecting";
      this.taskSocket = new WebSocket(this.taskSocketUrl());
      this.taskSocket.onopen = () => {
        this.socketStatus = "live";
      };
      this.taskSocket.onmessage = (event) => {
        try {
          const data = JSON.parse(event.data);
          if (data.status === "success") {
            const nextActive = data.tasks || [];
            const previousIds = new Set(this.activeTasks.map(task => task.id));
            const completed = this.activeTasks.some(task => previousIds.has(task.id) && !nextActive.some(next => next.id === task.id));
            this.activeTasks = nextActive;
            if (completed) this.fetchHistory(true);
            this.error = "";
            this.socketStatus = "live";
          }
        } catch (error) {
          console.log(error);
        }
      };
      this.taskSocket.onclose = () => {
        this.taskSocket = null;
        this.socketStatus = "reconnecting";
        this.scheduleReconnect();
      };
      this.taskSocket.onerror = () => {
        if (this.taskSocket) this.taskSocket.close();
      };
    },
    scheduleReconnect() {
      if (this.reconnectTimer) return;
      this.reconnectTimer = setTimeout(() => {
        this.reconnectTimer = null;
        this.connectTaskSocket();
      }, 1500);
    },
    closeTaskSocket() {
      if (this.reconnectTimer) {
        clearTimeout(this.reconnectTimer);
        this.reconnectTimer = null;
      }
      if (this.taskSocket) {
        const socket = this.taskSocket;
        this.taskSocket = null;
        socket.onclose = null;
        socket.close();
      }
    },
    async cancelTask(taskId) {
      try {
        const response = await fetch(`/api/tasks/${taskId}/cancel`, { method: "POST" });
        const data = await response.json();
        if (data.status === "error") this.error = data.message;
        await this.fetchTasks();
      } catch (error) {
        console.log(error);
        this.error = "network error.";
      }
    },
    statusClass(status) {
      if (status === "running") return "text-bg-primary";
      if (status === "queued") return "text-bg-info";
      if (status === "completed" || status === "success") return "text-bg-success";
      if (status === "cancelled" || status === "interrupted") return "text-bg-warning";
      if (status === "timed_out" || status === "failed" || status === "resource_exhausted") return "text-bg-danger";
      return "text-bg-secondary";
    },
    resultText(status, value, error) {
      if (status === "success") return value || "";
      if (status === "pending") return "Pending";
      return error || status;
    }
  },
  template: `
    <div class="task-page-header mb-3">
      <div class="d-flex align-items-center gap-2">
        <h1 class="h4 mb-0">Task Monitor</h1>
        <span class="badge" :class="socketStatus === 'live' ? 'text-bg-success' : 'text-bg-secondary'">{{ socketStatus }}</span>
      </div>
      <button class="btn btn-outline-success" type="button" @click="showHome">Back To Main Page</button>
    </div>

    <div v-if="error" class="alert alert-danger" role="alert">{{ error }}</div>

    <section class="mb-4">
      <div class="task-section-header mb-2">
        <h2 class="h5 mb-0">Active Tasks</h2>
        <button class="btn btn-outline-info btn-sm" @click="fetchTasks">Refresh</button>
      </div>
      <div v-if="runningTasks.length === 0" class="alert alert-secondary">No active tasks.</div>
      <div v-else class="table-responsive">
        <table class="table table-sm align-middle task-table">
          <thead>
            <tr>
              <th>ID</th>
              <th>Status</th>
              <th>Input Type</th>
              <th>Input</th>
              <th>Started At</th>
              <th>PD Notation</th>
              <th>Action</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="task in runningTasks" :key="task.id">
              <td data-label="ID">{{ task.id }}</td>
              <td data-label="Status">
                <span class="d-inline-flex align-items-center gap-2">
                  <span v-if="task.status === 'running'" class="spinner-border spinner-border-sm text-primary" role="status" aria-label="Computing"></span>
                  <span class="badge" :class="statusClass(task.status)">{{ task.status }}</span>
                </span>
              </td>
              <td data-label="Input Type">{{ task.input_type }}</td>
              <td data-label="Input"><pre class="mb-0 small task-cell">{{ task.input }}</pre></td>
              <td data-label="Started At">{{ task.started_at }}</td>
              <td data-label="PD Notation"><pre class="mb-0 small task-cell">{{ task.canonical_pd }}</pre></td>
              <td data-label="Action">
                <button class="btn btn-outline-danger btn-sm" :disabled="task.cancel_requested" @click="cancelTask(task.id)">
                  Terminate
                </button>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </section>

    <section>
      <h2 class="h5 mb-2">Completed Tasks</h2>
      <div v-if="finishedTasks.length === 0" class="alert alert-secondary">No completed tasks.</div>
      <div v-else class="table-responsive">
        <table class="table table-sm align-middle task-table">
          <thead>
            <tr>
              <th>ID</th>
              <th>Status</th>
              <th>Input Type</th>
              <th>Input</th>
              <th>Started At</th>
              <th>Ended At</th>
              <th>Knot Types</th>
              <th>HOMFLY-PT</th>
              <th>Khovanov</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="task in finishedTasks" :key="task.id">
              <td data-label="ID">{{ task.id }}</td>
              <td data-label="Status"><span class="badge" :class="statusClass(task.status)">{{ task.status }}</span></td>
              <td data-label="Input Type">{{ task.input_type }}</td>
              <td data-label="Input"><pre class="mb-0 small task-cell">{{ task.input }}</pre></td>
              <td data-label="Started At">{{ task.started_at }}</td>
              <td data-label="Ended At">{{ task.ended_at }}</td>
              <td data-label="Knot Types"><pre class="mb-0 small task-cell">{{ task.knot_types || task.error }}</pre></td>
              <td data-label="HOMFLY-PT">
                <span class="badge mb-1" :class="statusClass(task.homfly_status)">{{ task.homfly_status }}</span>
                <pre class="mb-0 small task-cell">{{ resultText(task.homfly_status, task.homfly_result, task.homfly_error) }}</pre>
              </td>
              <td data-label="Khovanov">
                <span class="badge mb-1" :class="statusClass(task.khovanov_status)">{{ task.khovanov_status }}</span>
                <pre class="mb-0 small task-cell">{{ resultText(task.khovanov_status, task.khovanov_result, task.khovanov_error) }}</pre>
              </td>
            </tr>
          </tbody>
        </table>
      </div>
      <div class="d-flex justify-content-center mt-3" v-if="historyHasMore">
        <button class="btn btn-outline-info btn-sm" :disabled="historyLoading" @click="fetchHistory(false)">
          {{ historyLoading ? "Loading" : "Load Older" }}
        </button>
      </div>
    </section>
  `
};
