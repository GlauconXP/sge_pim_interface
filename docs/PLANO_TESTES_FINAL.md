# Plano de testes finais do SGE

Use esta lista antes de considerar o protótipo pronto para a apresentação. Teste no ambiente local e não utilize dados pessoais reais. Marque cada item somente depois de observar o resultado.

## Preparação

- [ ] Banco `SGE_Encomendas` está disponível no SQL Server.
- [ ] Compilação por `compilar_sge_corrigido.bat` termina com código `0`.
- [ ] `executar_sge.bat` informa conexão ao SQL Server e servidor iniciado na porta `8000`.
- [ ] O navegador abre `http://127.0.0.1:8000` após `Ctrl + F5`.

## Autenticação e sessões

- [ ] Matrícula e senha válidas permitem entrar.
- [ ] Matrícula ou senha inválida não cria uma sessão.
- [ ] Conta inativa não permite novo login.
- [ ] Conta com senha temporária deve trocar a senha antes de usar as outras telas.
- [ ] Após trocar a senha, a nova senha permite entrar e a antiga deixa de funcionar.
- [ ] O botão Sair encerra a sessão.
- [ ] Se um funcionário for inativado/editado ou tiver senha redefinida, sua sessão anterior deixa de autorizar novas consultas.

## Funcionários

- [ ] Supervisora/administrador consegue cadastrar um funcionário com matrícula única.
- [ ] Matrícula duplicada é rejeitada com mensagem compreensível.
- [ ] Senha que não cumpre a política mínima é rejeitada.
- [ ] Novo funcionário consegue entrar e fazer a troca de senha exigida.
- [ ] Cargo e estado ativo/inativo são mostrados de acordo com o banco.
- [ ] Funcionário inativo não aparece como opção para atribuir a uma nova encomenda.

## Encomendas e histórico

- [ ] Usuário permitido cadastra uma encomenda com código único.
- [ ] Código duplicado é rejeitado sem duplicar dados.
- [ ] Encomenda com responsável mostra o nome correto após atualizar a página.
- [ ] A consulta SQL confirma que `FuncionarioResponsavelId` aponta para o funcionário escolhido.
- [ ] Busca textual localiza por código e outros campos suportados.
- [ ] Os filtros de status, tipo de envio e prioridade funcionam quando aplicáveis.
- [ ] Consulta por código conhecido abre os detalhes corretos.
- [ ] Histórico mostra as movimentações existentes no SQL Server.
- [ ] Avançar muda o status apenas para a próxima etapa permitida.
- [ ] Após avançar, atualizar a página mantém o status e a movimentação no banco.
- [ ] Tentativa de pular etapa ou operação proibida é rejeitada pelo servidor.

## Dashboard e relatórios

- [ ] Os totais do Dashboard correspondem ao status atual das encomendas no banco.
- [ ] A taxa de entrega é coerente com a proporção de encomendas em estado Entregue.
- [ ] A distribuição por status totaliza a quantidade de encomendas consideradas.
- [ ] Tempo médio em triagem mostra valor quando existem pares de movimentações necessários, ou indica ausência de dados.
- [ ] Cargo sem permissão não consegue acessar o endpoint de relatórios diretamente.

## Regressão e finalização

- [ ] O frontend não mostra dados antigos após `Ctrl + F5`.
- [ ] Nenhuma operação bem-sucedida depende apenas de modificar o DOM; o resultado persiste após recarregar.
- [ ] Os arquivos alterados foram revisados no GitHub Desktop.
- [ ] Commit tem uma mensagem específica e o Push origin termina sem erro.
- [ ] Há capturas de tela do login, Dashboard, lista de encomendas, detalhe/histórico, funcionários e relatórios para o relatório do PIM.

## Registro de resultado

Data do teste: ____________

Versão/commit testado: ____________

Problemas encontrados e correções: _________________________________________________

Responsáveis pelo teste: ___________________________________________________________
