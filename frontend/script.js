let employees = [];

async function carregarFuncionariosDoBanco() {
  const resposta = await fetch("/api/funcionarios");
  if (!resposta.ok) {
    throw new Error("Não foi possível consultar os funcionários no banco de dados.");
  }
  const dados = await resposta.json();
  if (!Array.isArray(dados)) {
    throw new Error("A resposta da lista de funcionários é inválida.");
  }
  employees = dados.map((item) => ({
    id: String(item.matricula ?? ""),
    name: item.nome ?? "Não informado",
    role: item.cargo ?? "",
    sector: item.setor ?? "",
    active: item.ativo === true,
  }));
}
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
    owner: String(item.responsavel ?? "").trim() || "Sem responsável",
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
const escaparHtml = (valor) =>
  String(valor ?? "").replace(/[&<>"']/g, (caractere) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    '"': "&quot;",
    "'": "&#39;",
  })[caractere]);
const badge = (s) => `<span class="badge ${statusClass(s)}">${escaparHtml(s)}</span>`;
const login = document.getElementById("login"),
  app = document.getElementById("app");
async function enter() {
  login.classList.add("hidden");
  app.classList.remove("hidden");

  try {
    await Promise.all([
      carregarEncomendasDoBanco(),
      carregarFuncionariosDoBanco(),
    ]);
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
        `<tr><td>${o.code}</td><td>${o.recipient}</td><td>${o.destination}</td><td>${badge(o.status)}</td><td>${o.owner}</td><td><button class="link track" data-code="${o.code}">Ver</button> · ${o.status === "Entregue" ? '<button class="link" disabled>Concluída</button>' : `<button class="link advance" data-code="${o.code}">Avançar</button>`}</td></tr>`,
    )
    .join("");
  document
    .querySelectorAll(".track")
    .forEach((b) => (b.onclick = () => openModal(b.dataset.code)));
  document.querySelectorAll(".advance").forEach((botao) => {
    botao.onclick = async () => {
      botao.disabled = true;
      await advance(botao.dataset.code);
      if (botao.isConnected) botao.disabled = false;
    };
  });
}
function renderEmployees() {
  document.getElementById("employeesTable").innerHTML = employees
    .map(
      (e) =>
        `<tr><td>${escaparHtml(e.id)}</td><td>${escaparHtml(e.name)}</td><td>${escaparHtml(e.role)}</td><td>${escaparHtml(e.sector)}</td><td>${e.active ? '<span class="badge b4">Ativo</span>' : '<span class="badge b1">Inativo</span>'}</td><td class="employee-actions"><button class="link employee-edit" data-matricula="${escaparHtml(e.id)}">Editar</button><button class="link employee-toggle" data-matricula="${escaparHtml(e.id)}">${e.active ? "Inativar" : "Ativar"}</button></td></tr>`,
    )
    .join("");

  document.querySelectorAll(".employee-edit").forEach((botao) => {
    botao.onclick = () => abrirModalFuncionario(botao.dataset.matricula);
  });
  document.querySelectorAll(".employee-toggle").forEach((botao) => {
    botao.onclick = () => alternarSituacaoFuncionario(botao.dataset.matricula);
  });

  const seletor = document.getElementById("owner");
  const valorAnterior = seletor.value;
  seletor.innerHTML =
    '<option value="">Sem responsável</option>' +
    employees
      .filter((e) => e.active)
      .map(
        (e) =>
          `<option value="${escaparHtml(e.id)}">${escaparHtml(e.name)} — ${escaparHtml(e.sector)}</option>`,
      )
      .join("");
  if ([...seletor.options].some((opcao) => opcao.value === valorAnterior)) {
    seletor.value = valorAnterior;
  }
}
function renderAll() {
  renderDash();
  renderOrders();
  renderEmployees();
}
document.getElementById("search").oninput = renderOrders;
document.getElementById("filter").onchange = renderOrders;
async function advance(code) {
  const encomenda = orders.find((item) => item.code === code);
  if (!encomenda) {
    alert("Não encontrei essa encomenda na lista.");
    return;
  }

  const indice = flow.indexOf(encomenda.status);
  if (indice < 0) {
    alert("O status atual dessa encomenda não é reconhecido.");
    return;
  }
  if (indice >= flow.length - 1) {
    alert("Essa encomenda já está marcada como entregue.");
    return;
  }

  const proximoStatus = flow[indice + 1];

  try {
    const resposta = await fetch("/api/encomendas/status", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ code: encomenda.code, status: proximoStatus }),
    });

    let resultado = {};
    try {
      resultado = await resposta.json();
    } catch (_) {
      // Mantém uma mensagem genérica caso o servidor não retorne JSON.
    }

    if (!resposta.ok) {
      throw new Error(
        resultado.erro || `Não foi possível atualizar o status (HTTP ${resposta.status}).`,
      );
    }

    // Só atualiza a tela depois que o SQL Server confirmar a alteração.
    await carregarEncomendasDoBanco();
    renderAll();
  } catch (erro) {
    console.error("Erro ao atualizar status:", erro);
    alert(erro.message || "Não foi possível atualizar o status da encomenda.");
  }
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
    ownerMatricula: String(dados.get("owner") ?? "").trim(),
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
async function openModal(code) {
  const encomenda = orders.find((item) => item.code === code);
  if (!encomenda) {
    alert("Não encontrei essa encomenda na lista.");
    return;
  }

  document.getElementById("modalCode").textContent = encomenda.code;
  document.getElementById("modalBody").innerHTML =
    `<div class="tracking"><div class="box"><b>Resumo</b><p><b>Destinatário:</b> ${escaparHtml(encomenda.recipient)}</p><p><b>Destino:</b> ${escaparHtml(encomenda.destination)}</p><p><b>Status atual:</b> ${badge(encomenda.status)}</p><p><b>Responsável:</b> ${escaparHtml(encomenda.owner)}</p><p><b>Tipo:</b> ${escaparHtml(encomenda.type)}</p><p><b>Prioridade:</b> ${escaparHtml(encomenda.priority)}</p></div><div class="box"><b>Histórico de movimentações</b><div id="historyContent"><p>Consultando histórico...</p></div></div></div>`;
  document.getElementById("modal").classList.remove("hidden");

  const conteudoHistorico = document.getElementById("historyContent");
  try {
    const resposta = await fetch(
      `/api/encomendas/historico?codigo=${encodeURIComponent(encomenda.code)}`,
    );
    const dados = await resposta.json();

    if (!resposta.ok) {
      throw new Error(dados.erro || "Não foi possível consultar o histórico.");
    }
    if (!Array.isArray(dados)) {
      throw new Error("A resposta do histórico é inválida.");
    }

    if (dados.length === 0) {
      conteudoHistorico.innerHTML =
        '<p>Nenhuma movimentação foi registrada para esta encomenda ainda.</p>';
      return;
    }

    conteudoHistorico.innerHTML = `<div class="timeline">${dados.map((movimentacao, indice) =>
      `<div class="${indice === dados.length - 1 ? "current" : "done"}"><span class="dot"></span><p><b>${escaparHtml(movimentacao.status)}</b><br><small>${escaparHtml(movimentacao.dataHora)}</small>${movimentacao.observacao ? `<br><small>${escaparHtml(movimentacao.observacao)}</small>` : ""}</p></div>`,
    ).join("")}</div>`;
  } catch (erro) {
    console.error("Erro ao carregar histórico:", erro);
    conteudoHistorico.innerHTML = `<p class="error">${escaparHtml(erro.message || "Erro ao consultar o histórico.")}</p>`;
  }
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
let matriculaOriginalEmEdicao = null;

function abrirModalFuncionario(matricula = null) {
  const funcionario = matricula === null
    ? null
    : employees.find((item) => item.id === matricula);
  if (matricula !== null && !funcionario) {
    alert("Não encontrei esse funcionário. Atualize a lista e tente novamente.");
    return;
  }

  const form = document.getElementById("employeeForm");
  form.reset();
  matriculaOriginalEmEdicao = funcionario ? funcionario.id : null;
  document.getElementById("employeeModalTitle").textContent =
    funcionario ? "Editar funcionário" : "Novo funcionário";
  document.getElementById("saveEmployeeButton").textContent =
    funcionario ? "Salvar alterações" : "Cadastrar funcionário";
  document.getElementById("employeeFormMessage").textContent = "";
  form.elements.namedItem("matricula").value = funcionario ? funcionario.id : "";
  form.elements.namedItem("nome").value = funcionario ? funcionario.name : "";
  form.elements.namedItem("cargo").value = funcionario ? funcionario.role : "";
  form.elements.namedItem("setor").value = funcionario ? funcionario.sector : "";
  form.elements.namedItem("ativo").value = funcionario ? String(funcionario.active) : "true";
  document.getElementById("employeeModal").classList.remove("hidden");
}

function fecharModalFuncionario() {
  document.getElementById("employeeModal").classList.add("hidden");
  document.getElementById("employeeFormMessage").textContent = "";
}

document.getElementById("newEmployee").onclick = () => abrirModalFuncionario();
document.getElementById("closeEmployeeModal").onclick = fecharModalFuncionario;
document.getElementById("cancelEmployeeModal").onclick = fecharModalFuncionario;
document.querySelector(".employee-backdrop").onclick = fecharModalFuncionario;

document.getElementById("employeeForm").addEventListener("submit", async (evento) => {
  evento.preventDefault();
  const form = evento.currentTarget;
  const dados = new FormData(form);
  const mensagem = document.getElementById("employeeFormMessage");
  const botaoSalvar = document.getElementById("saveEmployeeButton");
  const editando = matriculaOriginalEmEdicao !== null;
  const payload = {
    originalMatricula: matriculaOriginalEmEdicao,
    matricula: String(dados.get("matricula") ?? "").trim(),
    nome: String(dados.get("nome") ?? "").trim(),
    cargo: String(dados.get("cargo") ?? "").trim(),
    setor: String(dados.get("setor") ?? "").trim(),
    ativo: String(dados.get("ativo")) === "true",
  };

  if (!payload.matricula || !payload.nome || !payload.cargo || !payload.setor) {
    mensagem.textContent = "Preencha todos os campos antes de salvar.";
    return;
  }

  botaoSalvar.disabled = true;
  mensagem.textContent = "Salvando...";
  try {
    await enviarFuncionario(payload, editando ? "PUT" : "POST");
    await Promise.all([carregarFuncionariosDoBanco(), carregarEncomendasDoBanco()]);
    renderAll();
    fecharModalFuncionario();
    document.getElementById("employeeNotice").textContent = editando
      ? "Funcionário atualizado com sucesso."
      : "Funcionário cadastrado com sucesso.";
  } catch (erro) {
    console.error("Erro ao salvar funcionário:", erro);
    mensagem.textContent = erro.message || "Não foi possível salvar o funcionário.";
  } finally {
    botaoSalvar.disabled = false;
  }
});

async function enviarFuncionario(payload, metodo) {
  const resposta = await fetch("/api/funcionarios", {
    method: metodo,
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  let resultado = {};
  try {
    resultado = await resposta.json();
  } catch (_) {
    // Mantém mensagem padrão se a API não enviar JSON.
  }
  if (!resposta.ok) {
    throw new Error(resultado.erro || `Não foi possível salvar o funcionário (HTTP ${resposta.status}).`);
  }
  return resultado;
}

async function alternarSituacaoFuncionario(matricula) {
  const funcionario = employees.find((item) => item.id === matricula);
  if (!funcionario) return;
  const novaSituacao = !funcionario.active;
  const acao = novaSituacao ? "ativar" : "inativar";
  if (!confirm(`Deseja ${acao} o funcionário ${funcionario.name}?`)) return;

  try {
    await enviarFuncionario({
      originalMatricula: funcionario.id,
      matricula: funcionario.id,
      nome: funcionario.name,
      cargo: funcionario.role,
      setor: funcionario.sector,
      ativo: novaSituacao,
    }, "PUT");
    await Promise.all([carregarFuncionariosDoBanco(), carregarEncomendasDoBanco()]);
    renderAll();
    document.getElementById("employeeNotice").textContent =
      `Funcionário ${novaSituacao ? "ativado" : "inativado"} com sucesso.`;
  } catch (erro) {
    console.error("Erro ao alterar situação do funcionário:", erro);
    alert(erro.message || "Não foi possível alterar a situação do funcionário.");
  }
}

navigate("dashboard");
