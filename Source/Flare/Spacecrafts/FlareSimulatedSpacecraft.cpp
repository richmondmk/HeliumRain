
#include "../Flare.h"
#include "../Game/FlareSimulatedSector.h"
#include "../Game/FlareGame.h"
#include "../Game/FlareWorld.h"
#include "FlareSimulatedSpacecraft.h"


/*----------------------------------------------------
	Constructor
----------------------------------------------------*/

UFlareSimulatedSpacecraft::UFlareSimulatedSpacecraft(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}


void UFlareSimulatedSpacecraft::Load(const FFlareSpacecraftSave& Data)
{
	Game = Cast<UFlareCompany>(GetOuter())->GetGame();

	SpacecraftData = Data;

	// Load spacecraft description
	SpacecraftDescription = Game->GetSpacecraftCatalog()->Get(Data.Identifier);

	// Initialize damage system
	DamageSystem = NewObject<UFlareSimulatedSpacecraftDamageSystem>(this, UFlareSimulatedSpacecraftDamageSystem::StaticClass());
	DamageSystem->Initialize(this, &SpacecraftData);

	// Initialize navigation system
	NavigationSystem = NewObject<UFlareSimulatedSpacecraftNavigationSystem>(this, UFlareSimulatedSpacecraftNavigationSystem::StaticClass());
	NavigationSystem->Initialize(this, &SpacecraftData);

	// Initialize docking system
	DockingSystem = NewObject<UFlareSimulatedSpacecraftDockingSystem>(this, UFlareSimulatedSpacecraftDockingSystem::StaticClass());
	DockingSystem->Initialize(this, &SpacecraftData);

	// Initialize weapons system
	WeaponsSystem = NewObject<UFlareSimulatedSpacecraftWeaponsSystem>(this, UFlareSimulatedSpacecraftWeaponsSystem::StaticClass());
	WeaponsSystem->Initialize(this, &SpacecraftData);

	Game->GetGameWorld()->ClearFactories(this);
	Factories.Empty();


	for(int FactoryIndex = 0; FactoryIndex < SpacecraftDescription->Factories.Num(); FactoryIndex++)
	{
		FFlareFactorySave FactoryData;

		if (FactoryIndex < SpacecraftData.FactoryStates.Num())
		{
			FactoryData = SpacecraftData.FactoryStates[FactoryIndex];
		}
		else
		{
				FactoryData.Active = true;
				FactoryData.CostReserved = 0;
				FactoryData.ProductionBeginTime = GetGame()->GetGameWorld()->GetTime();
		}


		UFlareFactory* Factory = NewObject<UFlareFactory>(GetGame()->GetGameWorld(), UFlareFactory::StaticClass());
		Factory->Load(this, &SpacecraftDescription->Factories[FactoryIndex]->Data, FactoryData);
		Factories.Add(Factory);
		Game->GetGameWorld()->AddFactory(Factory);
	}
}

FFlareSpacecraftSave* UFlareSimulatedSpacecraft::Save()
{
	SpacecraftData.FactoryStates.Empty();

	for(int FactoryIndex = 0; FactoryIndex < Factories.Num(); FactoryIndex++)
	{
		SpacecraftData.FactoryStates.Add(*Factories[FactoryIndex]->Save());
	}

	return &SpacecraftData;
}


UFlareCompany* UFlareSimulatedSpacecraft::GetCompany()
{
	// TODO Cache
	return Game->GetGameWorld()->FindCompany(SpacecraftData.CompanyIdentifier);
}


EFlarePartSize::Type UFlareSimulatedSpacecraft::GetSize()
{
	return SpacecraftDescription->Size;
}

bool UFlareSimulatedSpacecraft::IsMilitary() const
{
	return IFlareSpacecraftInterface::IsMilitary(SpacecraftDescription);
}

bool UFlareSimulatedSpacecraft::IsStation() const
{
	return IFlareSpacecraftInterface::IsStation(SpacecraftDescription);
}

FName UFlareSimulatedSpacecraft::GetImmatriculation() const
{
	return SpacecraftData.Immatriculation;
}

UFlareSimulatedSpacecraftDamageSystem* UFlareSimulatedSpacecraft::GetDamageSystem() const
{
	return DamageSystem;
}

UFlareSimulatedSpacecraftNavigationSystem* UFlareSimulatedSpacecraft::GetNavigationSystem() const
{
	return NavigationSystem;
}

UFlareSimulatedSpacecraftDockingSystem* UFlareSimulatedSpacecraft::GetDockingSystem() const
{
	return DockingSystem;
}

UFlareSimulatedSpacecraftWeaponsSystem* UFlareSimulatedSpacecraft::GetWeaponsSystem() const
{
	return WeaponsSystem;
}

void UFlareSimulatedSpacecraft::SetSpawnMode(EFlareSpawnMode::Type SpawnMode)
{
	SpacecraftData.SpawnMode = SpawnMode;
}

/*----------------------------------------------------
	Gameplay
----------------------------------------------------*/


void UFlareSimulatedSpacecraft::SetCurrentSector(UFlareSimulatedSector* Sector)
{
	CurrentSector = Sector;

	// Mark the sector as visited
	if(Sector)
	{
		GetCompany()->VisitSector(Sector);
	}
}


/*----------------------------------------------------
	Resources
----------------------------------------------------*/

bool UFlareSimulatedSpacecraft::HasResources(FFlareResourceDescription* Resource, uint32 Quantity)
{
	int32 PresentQuantity = 0;


	if(Quantity == 0)
	{
		return true;
	}

	for(int CargoIndex = 0; CargoIndex < CargoBay.Num() ; CargoIndex++)
	{
		FFlareCargo* Cargo = &CargoBay[CargoIndex];
		if(Cargo->Resource == Resource)
		{
			PresentQuantity += Cargo->Quantity;
			if(PresentQuantity > Quantity)
			{
				return true;
			}
		}
	}
	return false;
}

bool UFlareSimulatedSpacecraft::TakeResources(FFlareResourceDescription* Resource, uint32 Quantity)
{
	uint32 QuantityToTake = Quantity;


	if(QuantityToTake == 0)
	{
		return true;
	}

	for(int CargoIndex = 0; CargoIndex < CargoBay.Num() ; CargoIndex++)
	{
		FFlareCargo& Cargo = CargoBay[CargoIndex];
		if(Cargo.Resource == Resource)
		{
			uint32 TakenQuantity = FMath::Min(Cargo.Quantity, QuantityToTake);
			if(TakenQuantity > 0)
			{
				Cargo.Quantity -= TakenQuantity;
				QuantityToTake -= TakenQuantity;

				if(Cargo.Quantity == 0)
				{
					Cargo.Resource = NULL;
				}

				if(QuantityToTake == 0)
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool UFlareSimulatedSpacecraft::GiveResources(FFlareResourceDescription* Resource, uint32 Quantity)
{
	uint32 QuantityToGive = Quantity;


	// First pass, fill already existing slots
	for(int CargoIndex = 0 ; CargoIndex < CargoBay.Num() ; CargoIndex++)
	{
		FFlareCargo& Cargo = CargoBay[CargoIndex];
		if (Resource == Cargo.Resource)
		{
			// Same resource
			uint32 AvailableCapacity = Cargo.Capacity - Cargo.Quantity;
			uint32 GivenQuantity = FMath::Min(AvailableCapacity, QuantityToGive);
			if(GivenQuantity > 0)
			{
				Cargo.Quantity += GivenQuantity;
				QuantityToGive -= GivenQuantity;

				if(QuantityToGive == 0)
				{
					return true;
				}
			}
		}
	}

	// Fill free cargo slots
	for(int CargoIndex = 0 ; CargoIndex < CargoBay.Num() ; CargoIndex++)
	{
		FFlareCargo& Cargo = CargoBay[CargoIndex];
		if (Cargo.Quantity == 0)
		{
			// Empty Cargo
			uint32 GivenQuantity = FMath::Min(Cargo.Capacity, QuantityToGive);
			if(GivenQuantity > 0)
			{
				Cargo.Quantity += GivenQuantity;
				Cargo.Resource = Resource;

				QuantityToGive -= GivenQuantity;

				if(QuantityToGive == 0)
				{
					return true;
				}
			}
			else
			{
				FLOGV("Zero sized cargo bay for %s", *GetImmatriculation().ToString())
			}

		}
	}

	return QuantityToGive > 0;
}
