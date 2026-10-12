USE SGE_Encomendas;
GO
SET NOCOUNT ON;
GO

/* Funcionários de demonstração para o protótipo acadêmico.
   O script só insere matrículas que ainda não existem; não altera cadastros existentes. */
IF NOT EXISTS (SELECT 1 FROM dbo.Funcionarios WHERE Matricula = N'001234')
BEGIN
    INSERT INTO dbo.Funcionarios (Matricula, Nome, Cargo, Setor, Ativo)
    VALUES (N'001234', N'João Silva', N'Atendente', N'Triagem', 1);
END;
GO

IF NOT EXISTS (SELECT 1 FROM dbo.Funcionarios WHERE Matricula = N'002345')
BEGIN
    INSERT INTO dbo.Funcionarios (Matricula, Nome, Cargo, Setor, Ativo)
    VALUES (N'002345', N'Maria Oliveira', N'Conferente', N'Separação', 1);
END;
GO

IF NOT EXISTS (SELECT 1 FROM dbo.Funcionarios WHERE Matricula = N'003456')
BEGIN
    INSERT INTO dbo.Funcionarios (Matricula, Nome, Cargo, Setor, Ativo)
    VALUES (N'003456', N'Carlos Souza', N'Entregador', N'Distribuição', 1);
END;
GO

IF NOT EXISTS (SELECT 1 FROM dbo.Funcionarios WHERE Matricula = N'004567')
BEGIN
    INSERT INTO dbo.Funcionarios (Matricula, Nome, Cargo, Setor, Ativo)
    VALUES (N'004567', N'Ana Lima', N'Supervisora', N'Operacional', 1);
END;
GO

IF NOT EXISTS (SELECT 1 FROM dbo.Funcionarios WHERE Matricula = N'005678')
BEGIN
    INSERT INTO dbo.Funcionarios (Matricula, Nome, Cargo, Setor, Ativo)
    VALUES (N'005678', N'Pedro Costa', N'Administrativo', N'Financeiro', 0);
END;
GO

SELECT Id, Matricula, Nome, Cargo, Setor, Ativo
FROM dbo.Funcionarios
ORDER BY Nome;
GO
