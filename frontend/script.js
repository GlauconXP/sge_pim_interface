const employees = [
  {
    id: "001234",
    name: "João Silva",
    role: "Atendente",
    sector: "Triagem",
    active: true,
  },
  {
    id: "002345",
    name: "Maria Oliveira",
    role: "Conferente",
    sector: "Separação",
    active: true,
  },
  {
    id: "003456",
    name: "Carlos Souza",
    role: "Entregador",
    sector: "Distribuição",
    active: true,
  },
  {
    id: "004567",
    name: "Ana Lima",
    role: "Supervisora",
    sector: "Operacional",
    active: true,
  },
  {
    id: "005678",
    name: "Pedro Costa",
    role: "Administrativo",
    sector: "Financeiro",
    active: false,
  },
];
const flow = ["Recebida", "Em triagem", "Separada", "Em distribuição", "Entregue"];
let orders = [];

async function carregarEncomendasDoBanco() {
  const resposta = await fetch("/api/encomendas");

  if (!resposta.ok) {
    throw new Error("Não foi possível consultar as encomendas.");
  }

  const dados = await resposta.json();

  if (!Array.isArray(dados)) {
    throw new Error("A resposta da API é inválida.");
  }

  orders = dados.map((item) => ({
    code: item.codigo ?? "",
    recipient: item.destinatario ?? "Não informado",
    destination: item.cidade ?? "Não informado",
    status: item.status ?? "Recebida",
    owner: "—",
    date: "—",
    address: "",
    type: "—",
    priority: "Normal",
  }));
}
const statusClass = (s) =>
  ({
    Recebida: "b1",
    "Em triagem": "b2",
    Separada: "b3",
    "Em distribuição": "b4",
    Entregue: "b4",
  })[s] || "b1";
const badge = (s) => `<span class="badge ${statusClass(s)}">${s}</span>`;
const login = document.getElementById("login"),
  app = document.getElementById("app");
async function enter() {
  login.classList.add("hidden");
  app.classList.remove("hidden");

  try {
    await carregarEncomendasDoBanco();
    renderAll();
  } catch (erro) {
    console.error("Erro ao carregar encomendas:", erro);

    orders = [];
    renderAll();

    alert(
      "Não foi possível carregar as encomendas. " +
      "Confira se o servidor C está funcionando."
    );
  }
}
document.getElementById("loginForm").addEventListener("submit", (e) => {
  e.preventDefault();
  if (
    !document.getElementById("user").value ||
    !document.getElementById("pass").value
  ) {
    document.getElementById("loginError").textContent =
      "Preencha matrícula e senha.";
    return;
  }
  enter();
});
document.getElementById("demo").onclick = () => {
  document.getElementById("user").value = "001234";
  document.getElementById("pass").value = "1234";
  enter();
};
document.getElementById("logout").onclick = () => {
  app.classList.add("hidden");
  login.classList.remove("hidden");
};
function navigate(p) {
  document
    .querySelectorAll(".page")
    .forEach((x) => x.classList.remove("active"));
  document.getElementById("page-" + p).classList.add("active");
  document
    .querySelectorAll(".nav[data-page]")
    .forEach((x) => x.classList.toggle("active", x.dataset.page === p));
  document.getElementById("title").textContent = {
    dashboard: "Dashboard",
    encomendas: "Encomendas",
    cadastro: "Cadastrar encomenda",
    funcionarios: "Funcionários",
    relatorios: "Relatórios",
  }[p];
}
document
  .querySelectorAll(".nav[data-page]")
  .forEach((b) => (b.onclick = () => navigate(b.dataset.page)));
document
  .querySelectorAll("[data-go]")
  .forEach((b) => (b.onclick = () => navigate(b.dataset.go)));
function renderDash() {
  document.getElementById("dashTable").innerHTML = orders
    .slice(0, 5)
    .map(
      (o) =>
        `<tr><td>${o.code}</td><td>${o.recipient}</td><td>${o.destination}</td><td>${badge(o.status)}</td></tr>`,
    )
    .join("");
}
function renderOrders() {
  const q = document.getElementById("search").value.toLowerCase(),
    f = document.getElementById("filter").value;
  const rows = orders.filter(
    (o) =>
      (f === "Todos" || o.status === f) &&
      `${o.code} ${o.recipient} ${o.destination}`.toLowerCase().includes(q),
  );
  document.getElementById("ordersTable").innerHTML = rows
    .map(
      (o) =>
        `<tr><td>${o.code}</td><td>${o.recipient}</td><td>${o.destination}</td><td>${badge(o.status)}</td><td>${o.owner}</td><td><button class="link track" data-code="${o.code}">Ver</button> · <button class="link advance" data-code="${o.code}">Avançar</button></td></tr>`,
    )
    .join("");
  document
    .querySelectorAll(".track")
    .forEach((b) => (b.onclick = () => openModal(b.dataset.code)));
  document
    .querySelectorAll(".advance")
    .forEach((b) => (b.onclick = () => advance(b.dataset.code)));
}
function renderEmployees() {
  document.getElementById("employeesTable").innerHTML = employees
    .map(
      (e) =>
        `<tr><td>${e.id}</td><td>${e.name}</td><td>${e.role}</td><td>${e.sector}</td><td>${e.active ? badge("Entregue") : '<span class="badge b1">Inativo</span>'}</td></tr>`,
    )
    .join("");
  document.getElementById("owner").innerHTML = employees
    .filter((e) => e.active)
    .map((e) => `<option>${e.name}</option>`)
    .join("");
}
function renderAll() {
  renderDash();
  renderOrders();
  renderEmployees();
}
document.getElementById("search").oninput = renderOrders;
document.getElementById("filter").onchange = renderOrders;
function advance(code) {
  const o = orders.find((x) => x.code === code),
    i = flow.indexOf(o.status);
  o.status = flow[Math.min(i + 1, flow.length - 1)];
  renderAll();
}
document.getElementById("packageForm").addEventListener("submit", async (e) => {
  e.preventDefault();

  const form = e.currentTarget;
  const dados = new FormData(form);
  const codigo = String(dados.get("code") ?? "").trim().toUpperCase();
  const mensagem = document.getElementById("success");
  mensagem.textContent = "";

  if (orders.some((encomenda) => encomenda.code.toUpperCase() === codigo)) {
    mensagem.textContent = "Já existe uma encomenda com esse código.";
    return;
  }

  const encomenda = {
    code: codigo,
    recipient: String(dados.get("recipient") ?? "").trim(),
    address: String(dados.get("address") ?? "").trim(),
    cep: String(dados.get("cep") ?? "").trim(),
    neighborhood: String(dados.get("neighborhood") ?? "").trim(),
    city: String(dados.get("city") ?? "").trim(),
    type: String(dados.get("type") ?? "SEDEX"),
    status: String(dados.get("status") ?? "Recebida"),
    priority: String(dados.get("priority") ?? "Normal"),
  };

  try {
    const resposta = await fetch("/api/encomendas", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(encomenda),
    });

    let resultado = {};
    try {
      resultado = await resposta.json();
    } catch (_) {
      // A API pode retornar uma resposta sem JSON em caso de erro.
    }

    if (!resposta.ok) {
      throw new Error(
        resultado.erro || `Não foi possível cadastrar (HTTP ${resposta.status}).`,
      );
    }

    // Recarrega a lista com os dados confirmados no SQL Server.
    await carregarEncomendasDoBanco();
    renderAll();
    form.reset();
    mensagem.textContent = "Encomenda cadastrada com sucesso no banco de dados.";
  } catch (erro) {
    console.error("Erro ao cadastrar encomenda:", erro);
    mensagem.textContent =
      erro.message || "Erro ao cadastrar. Confira se o servidor C está funcionando.";
  }
});
function openModal(code) {
  const o = orders.find((x) => x.code === code);
  document.getElementById("modalCode").textContent = o.code;
  const i = flow.indexOf(o.status);
  document.getElementById("modalBody").innerHTML =
    `<div class="tracking"><div class="box"><b>Resumo</b><p><b>Destinatário:</b> ${o.recipient}</p><p><b>Destino:</b> ${o.destination}</p><p><b>Status:</b> ${badge(o.status)}</p><p><b>Responsável:</b> ${o.owner}</p><p><b>Tipo:</b> ${o.type}</p><p><b>Prioridade:</b> ${o.priority}</p></div><div class="box"><b>Histórico</b><div class="timeline">${flow.map((s, n) => `<div class="${n < i ? "done" : n === i ? "current" : ""}"><span class="dot"></span><p><b>${s}</b><br><small>${n < i ? "Concluído" : n === i ? "Etapa atual" : "Pendente"}</small></p></div>`).join("")}</div></div></div>`;
  document.getElementById("modal").classList.remove("hidden");
}
document.getElementById("quickTrack").onclick = () => {
  if (orders.length === 0) {
    alert("Não há encomendas cadastradas para consultar.");
    return;
  }
  openModal(orders[0].code);
};
document.getElementById("closeModal").onclick = () =>
  document.getElementById("modal").classList.add("hidden");
document.querySelector(".backdrop").onclick = () =>
  document.getElementById("modal").classList.add("hidden");
document.getElementById("refresh").onclick = () =>
  (document.getElementById("delivery").textContent =
    82 + Math.floor(Math.random() * 14) + "%");
document.getElementById("newEmployee").onclick = () =>
  alert("Tela de cadastro de funcionário será implementada na próxima etapa.");
navigate("dashboard");
