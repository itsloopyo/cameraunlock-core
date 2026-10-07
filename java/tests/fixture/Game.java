package fixture;

/** The game's own main class in BootTests. */
public final class Game {
    private Game() {}

    public static void main(String[] arguments) {
        System.out.println("game ran with " + String.join(",", arguments));
    }
}
