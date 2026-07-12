export default {
  data() {
    return {
      website_title: "",
      author_info: "powered by <code>GGN_2015</code>, from Jilin University, CCST",
      last_build_info: ""
    };
  },
  async mounted() {
    this.navbarSetTitle(document.title);
  },
  methods: {
    navbarSetTitle(title) {
      this.website_title = title;
    },
    navbarSearch() {
      const searchContent = document.getElementById("search_content").value;
      alert(searchContent);
    },
    async getLastBuildInfo() {
      const response = await fetch("/api/last_build_info");
      return await response.text();
    },
    async toggleOffcanvas() {
      this.last_build_info = await this.getLastBuildInfo();
      document.getElementById("offcanvas_controller").click();
    }
  },
  template: `
    <nav class="navbar navbar-expand-lg bg-body-tertiary">
        <div class="container-fluid">
          <a class="navbar-brand" href="#">
            <img src="/img/logo.svg" width="30" height="30" class="d-inline-block align-top" alt="">
            <span class="navbar-brand-text">{{ website_title }}</span>
          </a>
          <button class="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#navbarSupportedContent" aria-controls="navbarSupportedContent" aria-expanded="false" aria-label="Toggle navigation">
            <span class="navbar-toggler-icon"></span>
          </button>
          <div class="collapse navbar-collapse" id="navbarSupportedContent">
            <ul class="navbar-nav me-auto mb-2 mb-lg-0">
              <li class="nav-item">
                <a class="nav-link active" aria-current="page" href="/">Home</a>
              </li>
              <li class="nav-item">
                <a class="nav-link" href="/tasks.html">Task Monitor</a>
              </li>
              <li class="nav-item">
                <a class="nav-link" href="#" @click="toggleOffcanvas">About Us</a>
              </li>
            </ul>
            <div class="d-flex" role="search">
              <input id="search_content" class="form-control me-2" type="search" placeholder="Knot Name" aria-label="Search" style="display: none;">
              <button class="btn btn-outline-success" @click="navbarSearch" style="display: none;">Search</button>
            </div>
          </div>
        </div>
    </nav>

    <button class="btn btn-primary" style="display: none;" id="offcanvas_controller" type="button" data-bs-toggle="offcanvas" data-bs-target="#offcanvasExample" aria-controls="offcanvasExample">
      Button with data-bs-target
    </button>

    <div class="offcanvas offcanvas-start" tabindex="-1" id="offcanvasExample" aria-labelledby="offcanvasExampleLabel">
      <div class="offcanvas-header">
        <h5 class="offcanvas-title" id="offcanvasExampleLabel">
          <img src="/img/logo.svg" width="30" height="30" class="d-inline-block align-top" alt="">
          knot-indexer-lab@jlu
        </h5>
        <button type="button" class="btn-close" data-bs-dismiss="offcanvas" aria-label="Close"></button>
      </div>
      <div class="offcanvas-body">
        <div>
          <p v-html="author_info"></p>
          <pre><code v-html="last_build_info"></code></pre>
        </div>
      </div>
    </div>
    `
};
