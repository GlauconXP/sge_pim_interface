USE SGE_Encomendas;
GO
SET NOCOUNT ON;
GO

/* Add columns without deleting existing employee data. */
IF COL_LENGTH('dbo.Funcionarios', 'SenhaSalt') IS NULL
    ALTER TABLE dbo.Funcionarios ADD SenhaSalt VARBINARY(16) NULL;
GO
IF COL_LENGTH('dbo.Funcionarios', 'SenhaHash') IS NULL
    ALTER TABLE dbo.Funcionarios ADD SenhaHash VARBINARY(32) NULL;
GO
IF COL_LENGTH('dbo.Funcionarios', 'SenhaDeveTrocar') IS NULL
BEGIN
    ALTER TABLE dbo.Funcionarios ADD SenhaDeveTrocar BIT NOT NULL
        CONSTRAINT DF_Funcionarios_SenhaDeveTrocar DEFAULT (1) WITH VALUES;
END
GO

/* Initial credentials for the five demo employees only. Each user gets a distinct salt.
   Existing passwords are never overwritten. Temporary password must be changed at first login. */
UPDATE dbo.Funcionarios
SET SenhaSalt = 0x142618843081A8F3C6ED4FBAED4F00A4,
    SenhaHash = 0xBA947EA2221BE4F0F6E21BCA0970ADA26985E2DD7856386ECCA79C3F2E96FF71,
    SenhaDeveTrocar = 1
WHERE Matricula = N'001234' AND SenhaHash IS NULL;
GO

UPDATE dbo.Funcionarios
SET SenhaSalt = 0x965695EBFF9B6D8C707CE7007D1339D3,
    SenhaHash = 0x68C8B4F0AA5A48392E714264DB7F72270C1339FB9DED2F24607B7ADD8FF52BDB,
    SenhaDeveTrocar = 1
WHERE Matricula = N'002345' AND SenhaHash IS NULL;
GO

UPDATE dbo.Funcionarios
SET SenhaSalt = 0xD4018EA3795F788E6B475168439BCDB6,
    SenhaHash = 0x8C03ECDE65F7C6EAF7D1AE9DB27BE73362C3BE9406EABEF44F6650FBF47C7A17,
    SenhaDeveTrocar = 1
WHERE Matricula = N'003456' AND SenhaHash IS NULL;
GO

UPDATE dbo.Funcionarios
SET SenhaSalt = 0x7E53F8EF42DBB0EDC3F429EBBEA3157B,
    SenhaHash = 0xE723A300F234FBFBF552C77BA3249BAC8B8ACEFDC1FCB0494F08516A9E11F9EE,
    SenhaDeveTrocar = 1
WHERE Matricula = N'004567' AND SenhaHash IS NULL;
GO

UPDATE dbo.Funcionarios
SET SenhaSalt = 0x9AE234E111AD76346033CFF122D0C1C0,
    SenhaHash = 0xFD30298BD62BF67325196BB693A0B2F92B96D51A9150441ED79192A6125142B3,
    SenhaDeveTrocar = 1
WHERE Matricula = N'005678' AND SenhaHash IS NULL;
GO

/* Review any employee who still needs a password. */
SELECT Matricula, Nome, Ativo,
       CASE WHEN SenhaHash IS NULL THEN N'Senha não configurada'
            WHEN SenhaDeveTrocar = 1 THEN N'Trocar no primeiro acesso'
            ELSE N'Configurada' END AS SituacaoSenha
FROM dbo.Funcionarios
ORDER BY Nome;
GO
