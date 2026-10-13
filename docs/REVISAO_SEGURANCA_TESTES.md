# Revisão de segurança e testes do SGE

## Alteração incluída

Quando uma supervisora ou pessoa autorizada edita um funcionário, o servidor identifica o ID do funcionário e invalida imediatamente as sessões existentes dessa conta. Isso reduz o risco de uma conta continuar usando permissões antigas após mudança de cargo, inativação ou redefinição de senha. A pessoa afetada deve entrar novamente; se a senha tiver sido redefinida, deve fazer a troca exigida.

## Testes manuais recomendados

Execute os testes em ambiente local de demonstração e use dados não sensíveis.

1. **Sem autenticação:** abrir `http://127.0.0.1:8000/api/encomendas` sem login deve retornar HTTP 401.
2. **Login inválido:** matrícula ou senha errada não deve criar sessão.
3. **Senha inicial:** uma conta com `SenhaDeveTrocar = 1` só deve conseguir acessar a rota de troca de senha até concluir a troca.
4. **Logout:** ao sair, a sessão antiga deve deixar de autorizar requisições.
5. **Permissões:** atendente não deve cadastrar/editar funcionários nem acessar relatórios restritos; supervisora pode fazê-lo.
6. **Revogação de sessão:** entre como funcionário de teste; em outra sessão de supervisão, inative esse funcionário ou mude seu cargo/redifina a senha. A sessão antiga deve ser rejeitada no próximo acesso à API.
7. **Fluxo de status:** tente pular uma etapa; a API deve rejeitar e manter o estado anterior.
8. **Persistência:** cadastre ou atualize uma encomenda e recarregue a página; os dados devem continuar no SQL Server.
9. **Dados inválidos:** matrícula duplicada, senha curta e código de encomenda inexistente devem ser rejeitados com mensagem controlada.
10. **Histórico:** avançar uma encomenda deve acrescentar uma movimentação e atualizar o status na mesma transação.

## Limitações conhecidas do protótipo local

- O servidor escuta em `127.0.0.1:8000` e usa HTTP. Não exponha a porta à rede nem publique o protótipo como serviço real sem HTTPS/TLS e uma revisão profissional.
- A string de conexão usa `TrustServerCertificate=Yes`, apropriado apenas para o ambiente local de desenvolvimento.
- Não existe limitação de tentativas de login neste protótipo; adicione bloqueio temporário e monitoramento antes de qualquer implantação real.
- As sessões ficam na memória do processo e expiram por inatividade; reiniciar o servidor encerra as sessões.
- As credenciais temporárias em `docs/auth_setup.sql` são somente para demonstração e devem ser trocadas no primeiro acesso. Nunca reutilize essa senha em outro sistema.

## Critério de encerramento

Marque a etapa como validada apenas depois de recompilar em Windows com `compilar_sge_corrigido.bat` (código de saída 0) e executar os testes acima. A análise estática não substitui esse teste no SQL Server real.
